using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;

namespace BlueOpenServer
{
    public class GithubAsset
    {
        public string name { get; set; } = "";
        public string browser_download_url { get; set; } = "";
    }

    public class GithubRelease
    {
        public string tag_name { get; set; } = "";
        public GithubAsset[] assets { get; set; } = Array.Empty<GithubAsset>();
    }

    public static class UpdateManager
    {
        private const string GithubApiUrl = "https://api.github.com/repos/serresident/blueopen/releases/latest";
        
        public const string CurrentVersion = "1.0.0";

        public static async Task<GithubRelease?> GetLatestReleaseAsync()
        {
            try
            {
                using var client = new HttpClient();
                // GitHub API requires a User-Agent header
                client.DefaultRequestHeaders.Add("User-Agent", "BlueOpenServer");
                
                var json = await client.GetStringAsync(GithubApiUrl);
                return JsonSerializer.Deserialize<GithubRelease>(json);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to fetch github release: {ex.Message}");
                return null;
            }
        }

        public static async Task<GithubRelease?> CheckForUpdatesAsync()
        {
            var release = await GetLatestReleaseAsync();
            if (release != null)
            {
                // GitHub tags usually have 'v' prefix, e.g. "v1.0.1"
                string version = release.tag_name.TrimStart('v', 'V');
                if (IsNewerVersion(version, CurrentVersion))
                {
                    return release;
                }
            }
            return null;
        }

        public static async Task<bool> DownloadAndInstallUpdateAsync(GithubRelease release)
        {
            try
            {
                // Ищем exe файл среди ассетов
                var exeAsset = Array.Find(release.assets, a => a.name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
                if (exeAsset == null)
                {
                    throw new Exception("Exe file not found in the release assets.");
                }

                string downloadUrl = exeAsset.browser_download_url;
                
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
