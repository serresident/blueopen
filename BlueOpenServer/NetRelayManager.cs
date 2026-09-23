using System;
using System.IO;
using System.IO.Pipes;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BlueOpenServer
{
    public class NetRelayManager : IDisposable
    {
        private readonly HttpClient _httpClient = new() { Timeout = TimeSpan.FromSeconds(15) };
        private readonly Func<AppConfig> _getConfig;
        private CancellationTokenSource? _cts;
        private Task? _pipeListenerTask;

        public event Action<string>? LogMessage;
        public event Action? UnlockConfirmed;

        public NetRelayManager(Func<AppConfig> getConfig)
        {
            _getConfig = getConfig;
        }

        public void Start()
        {
            Stop();
            _cts = new CancellationTokenSource();
            _pipeListenerTask = Task.Run(() => ListenForPipeTriggersAsync(_cts.Token));
            Log("[NetRelay] Background listener for BlueOpen Net lock-screen button started.");
        }

        public void Stop()
        {
            try
            {
                _cts?.Cancel();
                _cts?.Dispose();
                _cts = null;
            }
            catch { }
        }

        public string GetEffectiveTopic()
        {
            var config = _getConfig();
            if (!string.IsNullOrWhiteSpace(config.NetChannelId))
            {
                return config.NetChannelId.Trim();
            }

            // Derive deterministic topic from machine name and pairing password
            string raw = $"{Environment.MachineName}_{config.Password}_BlueOpenNet";
            using var sha = SHA256.Create();
            byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(raw));
            string hex = Convert.ToHexString(hash).ToLowerInvariant().Substring(0, 16);
            return $"bo_{hex}";
        }

        private async Task ListenForPipeTriggersAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    using var pipeServer = new NamedPipeServerStream(
                        "BlueOpenNetTriggerPipe",
                        PipeDirection.InOut,
                        1,
                        PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous
                    );

                    await pipeServer.WaitForConnectionAsync(token);

                    using var reader = new StreamReader(pipeServer, Encoding.UTF8, leaveOpen: true);
                    using var writer = new StreamWriter(pipeServer, Encoding.UTF8, leaveOpen: true) { AutoFlush = true };

                    string? line = await reader.ReadLineAsync(token);
                    if (line != null && line.Trim() == "TRIGGER_NET_UNLOCK")
                    {
                        var config = _getConfig();
                        if (!config.NetUnlockEnabled)
                        {
                            Log("[NetRelay] Lock screen requested Net Unlock, but feature is DISABLED in settings.");
                            await writer.WriteLineAsync("DISABLED");
                        }
                        else
                        {
                            Log("[NetRelay] Lock screen requested Net Unlock! Sending request to smartphone...");
                            await writer.WriteLineAsync("SENT");
                            _ = Task.Run(() => TriggerNetUnlockFlowAsync(token), token);
                        }
                    }

                    try
                    {
                        pipeServer.Disconnect();
                    }
                    catch { }
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Log($"[NetRelay] Pipe error: {ex.Message}");
                    await Task.Delay(1000, token);
                }
            }
        }

        public async Task<bool> TriggerNetUnlockFlowAsync(CancellationToken externalToken = default)
        {
            var config = _getConfig();
            string topic = GetEffectiveTopic();
            string serverUrl = string.IsNullOrWhiteSpace(config.NetServerUrl) ? "https://ntfy.sh" : config.NetServerUrl.TrimEnd('/');
            string requestId = Guid.NewGuid().ToString("N").Substring(0, 12);

            Log($"[NetRelay] Initiating Net Unlock request [ID: {requestId}] on channel '{topic}'...");

            try
            {
                // 1. Send push request to phone topic
                string postUrl = $"{serverUrl}/{topic}";
                var payload = new
                {
                    type = "UNLOCK_REQUEST",
                    requestId = requestId,
                    machine = Environment.MachineName,
                    user = config.WinUsername,
                    time = DateTime.UtcNow.ToString("o")
                };

                string json = JsonSerializer.Serialize(payload);
                using var requestMessage = new HttpRequestMessage(HttpMethod.Post, postUrl)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
                requestMessage.Headers.Add("Title", $"BlueOpen: Запрос на вход в {Environment.MachineName}");
                requestMessage.Headers.Add("Priority", "high");
                requestMessage.Headers.Add("Tags", "key,lock,computer");
                requestMessage.Headers.Add("Click", $"blueopen://unlock?reqId={requestId}&channel={topic}");

                var response = await _httpClient.SendAsync(requestMessage);
                if (!response.IsSuccessStatusCode)
                {
                    Log($"[NetRelay] Failed to publish request to {postUrl}: {response.StatusCode}");
                    return false;
                }

                Log($"[NetRelay] Request published! Waiting up to 60s for confirmation from phone...");

                // 2. Poll for confirmation on topic_ack
                string ackTopic = $"{topic}_ack";
                string ackUrl = $"{serverUrl}/{ackTopic}/json?poll=1&since=now";

                using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(60));
                using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(timeoutCts.Token, externalToken);

                var startTime = DateTime.UtcNow;
                while (!linkedCts.Token.IsCancellationRequested)
                {
                    try
                    {
                        var ackResp = await _httpClient.GetAsync(ackUrl, linkedCts.Token);
                        if (ackResp.IsSuccessStatusCode)
                        {
                            string body = await ackResp.Content.ReadAsStringAsync(linkedCts.Token);
                            var lines = body.Split('\n', StringSplitOptions.RemoveEmptyEntries);
                            foreach (var line in lines)
                            {
                                try
                                {
                                    using var doc = JsonDocument.Parse(line);
                                    if (doc.RootElement.TryGetProperty("event", out var ev) && ev.GetString() == "message")
                                    {
                                        if (doc.RootElement.TryGetProperty("message", out var msgProp))
                                        {
                                            string msgText = msgProp.GetString() ?? "";
                                            if (msgText.Contains(requestId) && (msgText.Contains("CONFIRMED") || msgText.Contains("UNLOCK_CONFIRMED")))
                                            {
                                                Log($"[NetRelay] [PASS] Confirmation received from smartphone for request {requestId}! Unlocking Windows...");
                                                UnlockConfirmed?.Invoke();
                                                return true;
                                            }
                                        }
                                    }
                                }
                                catch { }
                            }
                        }
                    }
                    catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested)
                    {
                        Log($"[NetRelay] Timeout (60s) reached without confirmation. Unlock aborted.");
                        return false;
                    }
                    catch (Exception ex)
                    {
                        Log($"[NetRelay] Error checking confirmation: {ex.Message}");
                    }

                    await Task.Delay(1500, linkedCts.Token);
                }
            }
            catch (Exception ex)
            {
                Log($"[NetRelay] Error in Net Unlock flow: {ex.Message}");
            }

            return false;
        }

        public async Task<bool> SendTestNotificationAsync()
        {
            var config = _getConfig();
            string topic = GetEffectiveTopic();
            string serverUrl = string.IsNullOrWhiteSpace(config.NetServerUrl) ? "https://ntfy.sh" : config.NetServerUrl.TrimEnd('/');

            try
            {
                string postUrl = $"{serverUrl}/{topic}";
                var payload = new
                {
                    type = "TEST",
                    machine = Environment.MachineName,
                    time = DateTime.UtcNow.ToString("o")
                };

                string json = JsonSerializer.Serialize(payload);
                using var requestMessage = new HttpRequestMessage(HttpMethod.Post, postUrl)
                {
                    Content = new StringContent(json, Encoding.UTF8, "application/json")
                };
                requestMessage.Headers.Add("Title", $"BlueOpen Net: Тестовое уведомление");
                requestMessage.Headers.Add("Priority", "default");
                requestMessage.Headers.Add("Tags", "bell,white_check_mark");

                var response = await _httpClient.SendAsync(requestMessage);
                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                Log($"[NetRelay] Test notification failed: {ex.Message}");
                return false;
            }
        }

        private void Log(string msg) => LogMessage?.Invoke(msg);

        public void Dispose()
        {
            Stop();
            _httpClient.Dispose();
        }
    }
}
