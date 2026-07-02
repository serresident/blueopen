using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;

namespace BlueOpenSetup
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("BlueOpen Server Installer");
            Console.WriteLine("=========================");
            
            try
            {
                // Install folder: %LOCALAPPDATA%\BlueOpen
                string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                string installFolder = Path.Combine(localAppData, "BlueOpen");
                string targetExe = Path.Combine(installFolder, "BlueOpenServer.exe");

                if (!Directory.Exists(installFolder))
                {
                    Directory.CreateDirectory(installFolder);
                }

                // Close running process if it exists
                var processes = Process.GetProcessesByName("BlueOpenServer");
                foreach (var p in processes)
                {
                    Console.WriteLine("Closing running BlueOpenServer instance...");
                    p.Kill();
                    p.WaitForExit(3000);
                }

                Console.WriteLine("Extracting application files...");

                // Read embedded payload
                var assembly = Assembly.GetExecutingAssembly();
                using (Stream stream = assembly.GetManifestResourceStream("BlueOpenSetup.Payload.BlueOpenServer.exe")!)
                {
                    if (stream == null)
                    {
                        Console.WriteLine("Error: Payload not found in the installer!");
                        Thread.Sleep(3000);
                        return;
                    }

                    using (FileStream fs = new FileStream(targetExe, FileMode.Create, FileAccess.Write))
                    {
                        stream.CopyTo(fs);
                    }
                }

                Console.WriteLine("Creating shortcuts...");

                string desktopPath = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                string startMenuPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs");
                
                string desktopLink = Path.Combine(desktopPath, "BlueOpen Server.lnk");
                string startMenuLink = Path.Combine(startMenuPath, "BlueOpen Server.lnk");

                CreateShortcut(targetExe, desktopLink);
                CreateShortcut(targetExe, startMenuLink);

                Console.WriteLine("Installation successful!");
                Console.WriteLine("Starting BlueOpen Server...");

                Process.Start(new ProcessStartInfo
                {
                    FileName = targetExe,
                    UseShellExecute = true
                });

            }
            catch (Exception ex)
            {
                Console.WriteLine("Installation failed: " + ex.Message);
                Console.WriteLine(ex.StackTrace);
                Console.WriteLine("Press any key to exit.");
                Console.ReadKey();
            }
        }

        static void CreateShortcut(string targetPath, string shortcutPath)
        {
            string ps = $"-NoProfile -Command \"$wshell = New-Object -ComObject WScript.Shell; $shortcut = $wshell.CreateShortcut('{shortcutPath}'); $shortcut.TargetPath = '{targetPath}'; $shortcut.WorkingDirectory = '{Path.GetDirectoryName(targetPath)}'; $shortcut.Save()\"";
            
            var psi = new ProcessStartInfo("powershell.exe", ps)
            {
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
                UseShellExecute = false
            };
            var p = Process.Start(psi);
            p?.WaitForExit();
        }
    }
}
