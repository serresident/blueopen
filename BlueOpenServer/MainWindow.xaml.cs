using System;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace BlueOpenServer
{
    public partial class MainWindow : Window
    {
        private readonly BluetoothServer _bluetoothServer;
        private AppConfig _config = new();
        private const string ConfigFileName = "config.json";
        private readonly string _configFilePath;

        // DLL Import for locking workstation natively
        [DllImport("user32.dll")]
        public static extern bool LockWorkStation();

        public MainWindow()
        {
            InitializeComponent();
            
            _configFilePath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, ConfigFileName);

            _bluetoothServer = new BluetoothServer();
            _bluetoothServer.LogMessage += OnServerLog;
            _bluetoothServer.UnlockRequested += OnRemoteUnlockRequested;
            _bluetoothServer.LockRequested += OnRemoteLockRequested;
        }

        private async void Window_Loaded(object sender, RoutedEventArgs e)
        {
            LoadConfig();
            
            // Apply config to UI
            TxtPassword.Text = _config.Password;
            ChkAutostart.IsChecked = _config.Autostart;
            ChkAutoLockOnBoot.IsChecked = _config.AutoLockOnBoot;

            TxtWinUsername.Text = _config.WinUsername;
            TxtWinDomain.Text = _config.WinDomain;
            TxtWinPassword.Password = _config.WinPassword;

            _bluetoothServer.SetPassword(_config.Password);

            // Auto-start Bluetooth server on app load
            try
            {
                await _bluetoothServer.StartAsync();
                UpdateStatusUI(true);
            }
            catch (Exception ex)
            {
                Log($"Failed to auto-start Bluetooth: {ex.Message}");
                UpdateStatusUI(false);
            }

            // If auto-lock on boot is enabled, lock Windows immediately
            if (_config.AutoLockOnBoot)
            {
                LockWorkstation();
            }
        }

        private void LoadConfig()
        {
            try
            {
                if (File.Exists(_configFilePath))
                {
                    string json = File.ReadAllText(_configFilePath);
                    _config = JsonSerializer.Deserialize<AppConfig>(json) ?? new AppConfig();
                }
                else
                {
                    _config = new AppConfig();
                    SaveConfig();
                }
            }
            catch (Exception ex)
            {
                Log($"Error loading configuration: {ex.Message}");
            }
        }

        private void SaveConfig()
        {
            try
            {
                string json = JsonSerializer.Serialize(_config, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(_configFilePath, json);
            }
            catch (Exception ex)
            {
                Log($"Error saving configuration: {ex.Message}");
            }
        }

        private void UpdateStatusUI(bool isRunning)
        {
            if (isRunning)
            {
                TxtStatus.Text = "Listening (RFCOMM)...";
                IndicatorDot.Fill = new SolidColorBrush(Color.FromRgb(76, 175, 80)); // Green
                BtnStart.IsEnabled = false;
                BtnStop.IsEnabled = true;
            }
            else
            {
                TxtStatus.Text = "Stopped";
                IndicatorDot.Fill = new SolidColorBrush(Color.FromRgb(211, 47, 47)); // Red
                BtnStart.IsEnabled = true;
                BtnStop.IsEnabled = false;
            }
        }

        private async void BtnStart_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                _bluetoothServer.SetPassword(_config.Password);
                await _bluetoothServer.StartAsync();
                UpdateStatusUI(true);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Could not start Bluetooth server: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                UpdateStatusUI(false);
            }
        }

        private void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            _bluetoothServer.Stop();
            UpdateStatusUI(false);
        }

        private void BtnSavePassword_Click(object sender, RoutedEventArgs e)
        {
            string newPassword = TxtPassword.Text.Trim();
            if (string.IsNullOrEmpty(newPassword))
            {
                MessageBox.Show("Password cannot be empty.", "Validation Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            _config.Password = newPassword;
            _bluetoothServer.SetPassword(newPassword);
            SaveConfig();
            
            MessageBox.Show("Security password saved and updated.", "Settings Saved", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void BtnSaveWinCredentials_Click(object sender, RoutedEventArgs e)
        {
            _config.WinUsername = TxtWinUsername.Text.Trim();
            _config.WinDomain = TxtWinDomain.Text.Trim();
            _config.WinPassword = TxtWinPassword.Password;
            SaveConfig();

            MessageBox.Show("Windows account login credentials saved successfully.", "Settings Saved", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void BtnLock_Click(object sender, RoutedEventArgs e)
        {
            LockWorkstation();
        }

        private void LockWorkstation()
        {
            Log("Locking workstation natively...");
            bool success = LockWorkStation();
            if (!success)
            {
                Log("Failed to lock workstation.");
            }
        }

        private void OnRemoteUnlockRequested()
        {
            Log("Remote unlock requested by phone. Injecting credentials to Named Pipe...");
            SendCredentialsToPipe();
        }

        private void SendCredentialsToPipe()
        {
            NamedPipeClientStream? pipeClient = null;
            try
            {
                Log("Connecting to Named Pipe...");
                pipeClient = new NamedPipeClientStream(".", "BlueOpenUnlockPipe", PipeDirection.InOut);
                pipeClient.Connect(2000);
                Log("Connected to Named Pipe.");

                string payload = $"{_config.WinDomain}\\{_config.WinUsername}:{_config.WinPassword}";
                byte[] payloadBytes = Encoding.UTF8.GetBytes(payload);

                Log("Sending credentials...");
                pipeClient.Write(payloadBytes, 0, payloadBytes.Length);
                pipeClient.Flush();
                Log("Credentials sent, waiting for confirmation...");

                byte[] responseBytes = new byte[16];
                int bytesRead = pipeClient.Read(responseBytes, 0, responseBytes.Length);
                if (bytesRead > 0)
                {
                    string response = Encoding.UTF8.GetString(responseBytes, 0, bytesRead).Trim();
                    Log($"Credential provider response: {response}");
                }
                else
                {
                    Log("Credential provider closed the pipe without a response.");
                }
            }
            catch (Exception ex)
            {
                Log($"Failed to write to Named Pipe: {ex.Message}. (Note: This is normal if the lock screen is not active and showing the BlueOpen tile.)");
            }
            finally
            {
                if (pipeClient != null)
                {
                    pipeClient.Close();
                    pipeClient.Dispose();
                }
            }
        }

        private void OnRemoteLockRequested()
        {
            Dispatcher.Invoke(() =>
            {
                Log("Remote lock requested by paired Bluetooth client.");
                LockWorkstation();
            });
        }

        private void OnServerLog(string message)
        {
            Dispatcher.Invoke(() =>
            {
                TxtLog.AppendText(message + Environment.NewLine);
                TxtLog.ScrollToEnd();
            });
        }

        private void Log(string message)
        {
            OnServerLog($"[{DateTime.Now:HH:mm:ss}] [System] {message}");
        }

        private void ChkAutostart_Checked(object sender, RoutedEventArgs e)
        {
            SetAutostart(true);
        }

        private void ChkAutostart_Unchecked(object sender, RoutedEventArgs e)
        {
            SetAutostart(false);
        }

        private void SetAutostart(bool enable)
        {
            _config.Autostart = enable;
            SaveConfig();

            try
            {
                string? appPath = Environment.ProcessPath;
                if (string.IsNullOrEmpty(appPath)) return;

                RegistryKey? rk = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Run", true);
                if (rk == null) return;

                if (enable)
                {
                    rk.SetValue("BlueOpen", $"\"{appPath}\"");
                    Log("Autostart enabled in registry.");
                }
                else
                {
                    rk.DeleteValue("BlueOpen", false);
                    Log("Autostart disabled in registry.");
                }
            }
            catch (Exception ex)
            {
                Log($"Registry write error: {ex.Message}");
            }
        }

        private void ChkAutoLockOnBoot_Checked(object sender, RoutedEventArgs e)
        {
            _config.AutoLockOnBoot = true;
            SaveConfig();
        }

        private void ChkAutoLockOnBoot_Unchecked(object sender, RoutedEventArgs e)
        {
            _config.AutoLockOnBoot = false;
            SaveConfig();
        }

        private void TxtPassword_GotFocus(object sender, RoutedEventArgs e) {}
        private void TxtPassword_LostFocus(object sender, RoutedEventArgs e) {}

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            _bluetoothServer.Stop();
        }
    }

    public class AppConfig
    {
        public string Password { get; set; } = "123456";
        public bool Autostart { get; set; } = false;
        public bool AutoLockOnBoot { get; set; } = false;
        public string WinUsername { get; set; } = Environment.UserName;
        public string WinDomain { get; set; } = Environment.UserDomainName;
        public string WinPassword { get; set; } = "";
    }
}