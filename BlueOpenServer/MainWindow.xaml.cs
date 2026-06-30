using System;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace BlueOpenServer
{
    public partial class MainWindow : Window
    {
        private readonly BluetoothServer _bluetoothServer;
        private LockWindow? _currentLockWindow;
        private AppConfig _config = new();
        private const string ConfigFileName = "config.json";
        private readonly string _configFilePath;

        public MainWindow()
        {
            InitializeComponent();
            
            // Set config path to local app directory
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

            // If auto-lock on boot is enabled, trigger lock screen immediately
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

        private void BtnLock_Click(object sender, RoutedEventArgs e)
        {
            LockWorkstation();
        }

        private void LockWorkstation()
        {
            if (_currentLockWindow != null) return; // Already locked

            Log("Locking workstation...");
            Hide(); // Hide settings dashboard while locked

            _currentLockWindow = new LockWindow(_config.Password);
            
            // Show Lock screen as modal
            bool? result = _currentLockWindow.ShowDialog();

            _currentLockWindow = null;
            Show(); // Show settings dashboard again once unlocked
            Log("Workstation unlocked.");
        }

        private void OnRemoteUnlockRequested()
        {
            if (_currentLockWindow != null)
            {
                Log("Remote unlock requested by paired Bluetooth client.");
                _currentLockWindow.RemoteUnlock();
            }
            else
            {
                Log("Unlock request received but screen is not locked.");
            }
        }

        private void OnRemoteLockRequested()
        {
            // Lock from phone
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

        private void TxtPassword_GotFocus(object sender, RoutedEventArgs e)
        {
            // Placeholder behavior if needed, otherwise ignore
        }

        private void TxtPassword_LostFocus(object sender, RoutedEventArgs e)
        {
            // Placeholder behavior if needed, otherwise ignore
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            // Clean up server
            _bluetoothServer.Stop();
        }
    }

    public class AppConfig
    {
        public string Password { get; set; } = "123456";
        public bool Autostart { get; set; } = false;
        public bool AutoLockOnBoot { get; set; } = false;
    }
}