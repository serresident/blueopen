using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;

namespace BlueOpenServer
{
    public class VersionInfo
    {
        public string version { get; set; } = "";
        public string downloadId { get; set; } = "";
        public string clientApkId { get; set; } = "";
    }

    public static class UpdateManager
    {
        // ВАЖНО: Замените эту ссылку на прямую ссылку (raw) на ваш файл version.json в Google Drive (или на GitHub/сервере).
        // Для Google Drive прямая ссылка на скачивание имеет вид: https://drive.google.com/uc?export=download&id=ВАШ_ID_ФАЙЛА
        private const string VersionUrl = "https://drive.google.com/uc?export=download&id=ЗАМЕНИТЕ_НА_ID_ФАЙЛА_VERSION_JSON";
        
        public const string CurrentVersion = "1.0.0";

        public static async Task<VersionInfo?> CheckForUpdatesAsync()
        {
            try
            {
                using var client = new HttpClient();
                // Add a dummy user agent
                client.DefaultRequestHeaders.Add("User-Agent", "BlueOpenServer Updater");
                
                var json = await client.GetStringAsync(VersionUrl);
                var info = JsonSerializer.Deserialize<VersionInfo>(json);
                
                if (info != null && IsNewerVersion(info.version, CurrentVersion))
                {
                    return info;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to check for updates: {ex.Message}");
            }
            return null;
        }

        public static async Task<bool> DownloadAndInstallUpdateAsync(VersionInfo versionInfo)
        {
            try
            {
                // Для Google Drive скачиваем по ID файла:
                string downloadUrl = $"https://drive.google.com/uc?export=download&id={versionInfo.downloadId}";
                
                string tempExePath = Path.Combine(Path.GetTempPath(), "BlueOpenServer_Update.exe");
                
                using (var client = new HttpClient())
                {
                    var response = await client.GetAsync(downloadUrl);
                    response.EnsureSuccessStatusCode();
                    
                    using (var fs = new FileStream(tempExePath, FileMode.Create, FileAccess.Write, FileShare.None))
                    {
                        await response.Content.CopyToAsync(fs);
                    }
                }

                // Создаем и запускаем .bat скрипт для подмены текущего .exe
                string currentExePath = Process.GetCurrentProcess().MainModule?.FileName ?? "";
                if (string.IsNullOrEmpty(currentExePath)) return false;

                string batPath = Path.Combine(Path.GetTempPath(), "BlueOpenUpdater.bat");
                
                string batContent = $@"
@echo off
echo Updating BlueOpen Server... Please wait.
timeout /t 2 /nobreak >nul
del ""{currentExePath}""
move /Y ""{tempExePath}"" ""{currentExePath}""
start """" ""{currentExePath}""
del ""%~f0""
";
                File.WriteAllText(batPath, batContent);

                // Запускаем bat в скрытом окне
                var psi = new ProcessStartInfo
                {
                    FileName = batPath,
                    UseShellExecute = true,
                    CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden
                };
                Process.Start(psi);
                
                // Закрываем текущее приложение, чтобы bat скрипт мог его заменить
                System.Windows.Application.Current.Shutdown();
                return true;
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Ошибка при скачивании обновления: {ex.Message}", "Ошибка обновления", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
                return false;
            }
        }

        private static bool IsNewerVersion(string newVersion, string currentVersion)
        {
            if (Version.TryParse(newVersion, out Version? nv) && Version.TryParse(currentVersion, out Version? cv))
            {
                return nv > cv;
            }
            return false;
        }
    }
}
