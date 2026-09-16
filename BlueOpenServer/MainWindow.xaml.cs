using System;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using QRCoder;
using System.Drawing;
using System.Drawing.Imaging;
using Forms = System.Windows.Forms;
using Color = System.Windows.Media.Color;
using MessageBox = System.Windows.MessageBox;

namespace BlueOpenServer
{
    public partial class MainWindow : Window
    {
        private readonly BluetoothServer _bluetoothServer;
        private AppConfig _config = new();
        private const string ConfigFileName = "config.json";
        private readonly string _configFilePath;

        private Forms.NotifyIcon? _notifyIcon;
        private Forms.ToolStripMenuItem? _menuItemServerActive;
        private Forms.ToolStripMenuItem? _menuItemAutostart;
        private bool _isExiting = false;
        private bool _shownTrayTip = false;

        // DLL Import for locking workstation natively
        [DllImport("user32.dll")]
        public static extern bool LockWorkStation();

        public MainWindow()
        {
            InitializeComponent();
            
            string commonData = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
            string appFolder = Path.Combine(commonData, "BlueOpen");
            if (!Directory.Exists(appFolder))
            {
                try { Directory.CreateDirectory(appFolder); } catch { }
            }
            _configFilePath = Path.Combine(appFolder, ConfigFileName);

            // Migrate old configs if they exist
            string userAppData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            string oldUserConfig = Path.Combine(userAppData, "BlueOpen", ConfigFileName);
            string oldBaseConfig = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, ConfigFileName);

            if (!File.Exists(_configFilePath))
            {
                if (File.Exists(oldUserConfig))
                {
                    try { File.Copy(oldUserConfig, _configFilePath); } catch { }
                }
                else if (File.Exists(oldBaseConfig))
                {
                    try { File.Copy(oldBaseConfig, _configFilePath); } catch { }
                }
            }

            _bluetoothServer = new BluetoothServer();
            _bluetoothServer.LogMessage += OnServerLog;
            _bluetoothServer.UnlockRequested += OnRemoteUnlockRequested;
            _bluetoothServer.LockRequested += OnRemoteLockRequested;

            StateChanged += MainWindow_StateChanged;

            InitializeTrayIcon();
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

            // Setup Version UI
            TxtVersion.Text = $"v{UpdateManager.CurrentVersion}";

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
            string json = JsonSerializer.Serialize(_config, new JsonSerializerOptions { WriteIndented = true });

            // 1. Save to ProgramData (shared with Credential Provider)
            try
            {
                string? dir = Path.GetDirectoryName(_configFilePath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                File.WriteAllText(_configFilePath, json);
            }
            catch (Exception ex)
            {
                Log($"Notice: Could not write shared config to ProgramData: {ex.Message}");
            }

            // 2. Save to User AppData as reliable backup
            try
            {
                string userAppData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
                string userConfigDir = Path.Combine(userAppData, "BlueOpen");
                if (!Directory.Exists(userConfigDir))
                {
                    Directory.CreateDirectory(userConfigDir);
                }
                File.WriteAllText(Path.Combine(userConfigDir, ConfigFileName), json);
            }
            catch (Exception ex)
            {
                Log($"Error saving user config: {ex.Message}");
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
            UpdateTrayMenuState();
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

        private async void BtnCheckUpdates_Click(object sender, RoutedEventArgs e)
        {
            BtnCheckUpdates.IsEnabled = false;
            BtnCheckUpdates.Content = "Checking...";
            
            try
            {
                var updateInfo = await UpdateManager.CheckForUpdatesAsync();
                
                if (updateInfo != null)
                {
                    var result = MessageBox.Show($"New version {updateInfo.tag_name} is available!\n\nDo you want to update now?", "Update Available", MessageBoxButton.YesNo, MessageBoxImage.Information);
                    if (result == MessageBoxResult.Yes)
                    {
                        BtnCheckUpdates.Content = "Downloading...";
                        bool success = await UpdateManager.DownloadAndInstallUpdateAsync(updateInfo);
                        if (!success)
                        {
                            BtnCheckUpdates.Content = "Update Failed";
                        }
                    }
                    else
                    {
                        BtnCheckUpdates.Content = "Check Updates";
                    }
                }
                else
                {
                    MessageBox.Show("You are using the latest version.", "Up to date", MessageBoxButton.OK, MessageBoxImage.Information);
                    BtnCheckUpdates.Content = "Check Updates";
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Update check failed: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                BtnCheckUpdates.Content = "Check Updates";
            }
            finally
            {
                BtnCheckUpdates.IsEnabled = true;
            }
        }

        private async void BtnGetClient_Click(object sender, RoutedEventArgs e)
        {
            BtnGetClient.IsEnabled = false;
            BtnGetClient.Content = "Loading link...";

            try
            {
                var release = await UpdateManager.GetLatestReleaseAsync();
                
                if (release != null && !string.IsNullOrEmpty(release.apk_download_url))
                {
                    string finalDownloadUrl = release.apk_download_url;
                    
                    // Generate QR Code
                    using (QRCodeGenerator qrGenerator = new QRCodeGenerator())
                    using (QRCodeData qrCodeData = qrGenerator.CreateQrCode(finalDownloadUrl, QRCodeGenerator.ECCLevel.Q))
                    using (QRCode qrCode = new QRCode(qrCodeData))
                    using (Bitmap qrBitmap = qrCode.GetGraphic(20))
                    {
                        ImgQrCode.Source = BitmapToImageSource(qrBitmap);
                        QrCodeContainer.Visibility = Visibility.Visible;
                    }
                    
                    BtnGetClient.Content = "Scan QR Code Below";
                }
                else
                {
                    MessageBox.Show("Could not find a valid release on GitHub.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    BtnGetClient.Content = "Get Android Client";
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to generate QR Code: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                BtnGetClient.Content = "Get Android Client";
            }
            finally
            {
                BtnGetClient.IsEnabled = true;
            }
        }

        private BitmapImage BitmapToImageSource(Bitmap bitmap)
        {
            using (MemoryStream memory = new MemoryStream())
            {
                bitmap.Save(memory, ImageFormat.Png);
                memory.Position = 0;
                BitmapImage bitmapImage = new BitmapImage();
                bitmapImage.BeginInit();
                bitmapImage.StreamSource = memory;
                bitmapImage.CacheOption = BitmapCacheOption.OnLoad;
                bitmapImage.EndInit();
                return bitmapImage;
            }
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
            UpdateTrayMenuState();
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
            if (!_isExiting)
            {
                e.Cancel = true;
                Hide();
                ShowTrayBalloonTip();
            }
            else
            {
                _bluetoothServer.Stop();
                if (_notifyIcon != null)
                {
                    _notifyIcon.Visible = false;
                    _notifyIcon.Dispose();
                }
            }
        }

        private void InitializeTrayIcon()
        {
            _notifyIcon = new Forms.NotifyIcon();
            _notifyIcon.Text = "BlueOpen Server";
            
            System.Drawing.Icon? appIcon = null;
            try
            {
                var streamInfo = System.Windows.Application.GetResourceStream(new Uri("pack://application:,,,/app_icon.ico"));
                if (streamInfo != null)
                {
                    appIcon = new System.Drawing.Icon(streamInfo.Stream);
                }
            }
            catch (Exception ex)
            {
                Log($"Error loading application icon from resources: {ex.Message}");
            }
            _notifyIcon.Icon = appIcon ?? System.Drawing.SystemIcons.Application;
            _notifyIcon.Visible = true;

            var contextMenu = new Forms.ContextMenuStrip();

            var menuItemRestore = new Forms.ToolStripMenuItem("Открыть BlueOpen", null, (s, e) => RestoreWindow());
            menuItemRestore.Font = new System.Drawing.Font(menuItemRestore.Font, System.Drawing.FontStyle.Bold);

            _menuItemServerActive = new Forms.ToolStripMenuItem("Bluetooth сервер активен", null, async (s, e) => await ToggleServerFromTray());
            _menuItemServerActive.CheckOnClick = false;

            _menuItemAutostart = new Forms.ToolStripMenuItem("Автозапуск с Windows", null, (s, e) => ToggleAutostartFromTray());
            _menuItemAutostart.CheckOnClick = false;

            var menuItemExit = new Forms.ToolStripMenuItem("Выход", null, (s, e) => ExitApplication());

            contextMenu.Items.Add(menuItemRestore);
            contextMenu.Items.Add(new Forms.ToolStripSeparator());
            contextMenu.Items.Add(_menuItemServerActive);
            contextMenu.Items.Add(_menuItemAutostart);
            contextMenu.Items.Add(new Forms.ToolStripSeparator());
            contextMenu.Items.Add(menuItemExit);

            _notifyIcon.ContextMenuStrip = contextMenu;
            _notifyIcon.DoubleClick += (s, e) => RestoreWindow();
            
            UpdateTrayMenuState();
        }

        private void RestoreWindow()
        {
            Show();
            WindowState = WindowState.Normal;
            Activate();
        }

        private async Task ToggleServerFromTray()
        {
            if (_bluetoothServer.IsRunning)
            {
                _bluetoothServer.Stop();
                UpdateStatusUI(false);
            }
            else
            {
                try
                {
                    _bluetoothServer.SetPassword(_config.Password);
                    await _bluetoothServer.StartAsync();
                    UpdateStatusUI(true);
                }
                catch (Exception ex)
                {
                    System.Windows.MessageBox.Show($"Could not start Bluetooth server: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    UpdateStatusUI(false);
                }
            }
        }

        private void ToggleAutostartFromTray()
        {
            ChkAutostart.IsChecked = ChkAutostart.IsChecked != true;
        }

        private void ExitApplication()
        {
            _isExiting = true;
            System.Windows.Application.Current.Shutdown();
        }

        private void TitleBar_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ClickCount == 2)
            {
                if (WindowState == WindowState.Normal)
                    WindowState = WindowState.Maximized;
                else
                    WindowState = WindowState.Normal;
            }
            else
            {
                DragMove();
            }
        }

        private void BtnMinimize_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void BtnClose_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void MainWindow_StateChanged(object? sender, EventArgs e)
        {
            if (WindowState == WindowState.Minimized)
            {
                Hide();
                ShowTrayBalloonTip();
            }
        }

        private void ShowTrayBalloonTip()
        {
            if (!_shownTrayTip && _notifyIcon != null)
            {
                _notifyIcon.ShowBalloonTip(3000, "BlueOpen Server", "Приложение свернуто в системный трей и продолжает работать.", Forms.ToolTipIcon.Info);
                _shownTrayTip = true;
            }
        }

        private void UpdateTrayMenuState()
        {
            if (_menuItemServerActive != null)
            {
                _menuItemServerActive.Checked = _bluetoothServer.IsRunning;
            }
            if (_menuItemAutostart != null)
            {
                _menuItemAutostart.Checked = _config.Autostart;
            }
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