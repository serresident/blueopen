using System;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;

namespace BlueOpenServer
{
    public partial class LockWindow : Window
    {
        private readonly DispatcherTimer _clockTimer;
        private readonly string _storedPassword;
        private bool _isUnlocking = false;

        public LockWindow(string storedPassword)
        {
            InitializeComponent();
            _storedPassword = storedPassword;

            // Timer to update clock and date
            _clockTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(1)
            };
            _clockTimer.Tick += ClockTimer_Tick;

            // Prevent window deactivation (forces focus back)
            Deactivated += LockWindow_Deactivated;
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            // Activate clock
            UpdateDateTime();
            _clockTimer.Start();

            // Set up Low Level Keyboard Hook to block Windows escape keys
            KeyboardHook.SetHook();

            // Focus password box immediately
            PasswordInput.Focus();
        }

        private void ClockTimer_Tick(object? sender, EventArgs e)
        {
            UpdateDateTime();
        }

        private void UpdateDateTime()
        {
            TxtClock.Text = DateTime.Now.ToString("HH:mm");
            TxtDate.Text = DateTime.Now.ToString("dddd, MMMM dd");
        }

        private void LockWindow_Deactivated(object? sender, EventArgs e)
        {
            if (!_isUnlocking)
            {
                // Force window back to topmost and active if user tries to focus elsewhere
                try
                {
                    Activate();
                    Topmost = true;
                    Focus();
                }
                catch
                {
                    // Ignore transient activation errors
                }
            }
        }

        private void PasswordInput_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter)
            {
                VerifyAndUnlock();
            }
            else
            {
                TxtError.Visibility = Visibility.Collapsed;
            }
        }

        private void Unlock_Click(object sender, RoutedEventArgs e)
        {
            VerifyAndUnlock();
        }

        private void VerifyAndUnlock()
        {
            if (PasswordInput.Password == _storedPassword)
            {
                UnlockSession();
            }
            else
            {
                TxtError.Visibility = Visibility.Visible;
                PasswordInput.Password = "";
                PasswordInput.Focus();
            }
        }

        public void RemoteUnlock()
        {
            // Bluetooth runs on background threads, marshalling to UI thread
            Dispatcher.Invoke(UnlockSession);
        }

        private void UnlockSession()
        {
            _isUnlocking = true;
            _clockTimer.Stop();
            KeyboardHook.Unhook();
            DialogResult = true; // Signals main window that we unlocked
            Close();
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            // Block closing unless we explicitly authorized it
            if (!_isUnlocking)
            {
                e.Cancel = true;
            }
        }
    }
}
