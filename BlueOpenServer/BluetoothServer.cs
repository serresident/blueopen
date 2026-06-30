using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Rfcomm;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;

namespace BlueOpenServer
{
    public class BluetoothServer
    {
        // Custom Service UUID for RFCOMM
        private static readonly Guid ServiceUuid = new Guid("4a982c5e-0c12-40f4-8a48-4a5f4a7c1b52");

        private RfcommServiceProvider? _provider;
        private StreamSocketListener? _listener;
        private string _password = "";
        private bool _isRunning = false;

        public event Action? UnlockRequested;
        public event Action? LockRequested;
        public event Action<string>? LogMessage;

        public bool IsRunning => _isRunning;

        public void SetPassword(string password)
        {
            _password = password;
        }

        public async Task StartAsync()
        {
            if (_isRunning) return;

            try
            {
                Log("Starting Bluetooth RFCOMM service...");

                // Create the RfcommServiceProvider
                _provider = await RfcommServiceProvider.CreateAsync(RfcommServiceId.FromUuid(ServiceUuid));

                // Create the StreamSocketListener
                _listener = new StreamSocketListener();
                _listener.ConnectionReceived += OnConnectionReceived;

                // Bind the listener to the service provider's connection parameters
                await _listener.BindServiceNameAsync(
                    _provider.ServiceId.AsString(), 
                    SocketProtectionLevel.PlainSocket);

                // Start advertising
                _provider.StartAdvertising(_listener);
                _isRunning = true;

                Log($"Bluetooth service started. UUID: {ServiceUuid}");
            }
            catch (Exception ex)
            {
                Log($"Error starting Bluetooth service: {ex.Message}");
                Stop();
                throw;
            }
        }

        public void Stop()
        {
            if (!_isRunning) return;

            try
            {
                Log("Stopping Bluetooth service...");
                _provider?.StopAdvertising();
                _provider = null;

                if (_listener != null)
                {
                    _listener.ConnectionReceived -= OnConnectionReceived;
                    _listener.Dispose();
                    _listener = null;
                }

                _isRunning = false;
                Log("Bluetooth service stopped.");
            }
            catch (Exception ex)
            {
                Log($"Error stopping service: {ex.Message}");
            }
        }

        private async void OnConnectionReceived(StreamSocketListener sender, StreamSocketListenerConnectionReceivedEventArgs args)
        {
            StreamSocket socket = args.Socket;
            Log($"Device connected: {socket.Information.RemoteAddress.DisplayName}");

            try
            {
                using (var reader = new DataReader(socket.InputStream))
                using (var writer = new DataWriter(socket.OutputStream))
                {
                    reader.InputStreamOptions = InputStreamOptions.Partial;

                    // 1. Generate challenge nonce
                    string nonce = Guid.NewGuid().ToString("N");
                    Log($"Generated challenge: {nonce}");

                    // Send challenge to client
                    writer.WriteString(nonce + "\n");
                    await writer.StoreAsync();

                    // 2. Read client response (we expect "hash:command\n")
                    string response = await ReadLineAsync(reader);
                    Log($"Received response from client: {response}");

                    if (string.IsNullOrEmpty(response) || !response.Contains(":"))
                    {
                        Log("Invalid response format. Expected 'hash:command'.");
                        writer.WriteString("FAIL\n");
                        await writer.StoreAsync();
                        return;
                    }

                    string[] parts = response.Split(':');
                    string clientHash = parts[0].Trim();
                    string command = parts[1].Trim().ToUpper();

                    // 3. Verify client hash
                    string expectedHash = ComputeSha256(nonce + _password);

                    if (string.Equals(clientHash, expectedHash, StringComparison.OrdinalIgnoreCase))
                    {
                        Log($"Authentication successful! Command: {command}");
                        writer.WriteString("OK\n");
                        await writer.StoreAsync();

                        if (command == "UNLOCK")
                        {
                            UnlockRequested?.Invoke();
                        }
                        else if (command == "LOCK")
                        {
                            LockRequested?.Invoke();
                        }
                    }
                    else
                    {
                        Log("Authentication failed. Hashes do not match.");
                        writer.WriteString("FAIL\n");
                        await writer.StoreAsync();
                    }
                }
            }
            catch (Exception ex)
            {
                Log($"Error handling client connection: {ex.Message}");
            }
            finally
            {
                socket.Dispose();
            }
        }

        private async Task<string> ReadLineAsync(DataReader reader)
        {
            StringBuilder sb = new StringBuilder();
            while (true)
            {
                // Load at least 1 byte
                uint bytesLoaded = await reader.LoadAsync(1);
                if (bytesLoaded == 0) break;

                char c = (char)reader.ReadByte();
                if (c == '\n') break;
                if (c != '\r')
                {
                    sb.Append(c);
                }
            }
            return sb.ToString();
        }

        private string ComputeSha256(string input)
        {
            using (SHA256 sha256 = SHA256.Create())
            {
                byte[] bytes = sha256.ComputeHash(Encoding.UTF8.GetBytes(input));
                StringBuilder builder = new StringBuilder();
                foreach (byte b in bytes)
                {
                    builder.Append(b.ToString("x2"));
                }
                return builder.ToString();
            }
        }

        private void Log(string message)
        {
            LogMessage?.Invoke($"[{DateTime.Now:HH:mm:ss}] {message}");
        }
    }
}
