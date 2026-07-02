# Ensure running as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "This script must be run as Administrator! Requesting elevation..."
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

$workspaceDir = $PSScriptRoot
$targetDir = "C:\Program Files\BlueOpen"
$srcServerDir = "$workspaceDir\BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0"
$srcDll = "$workspaceDir\BlueOpenProvider\bin\x64\Release\BlueOpenProvider.dll"

Write-Host "--- BlueOpen Installer ---" -ForegroundColor Cyan

# 1. Create Target Directory
if (-not (Test-Path $targetDir)) {
    Write-Host "Creating target folder: $targetDir"
    New-Item -ItemType Directory -Path $targetDir | Out-Null
}

# 2. Copy WPF Server Application files
Write-Host "Copying BlueOpen Server files..." -ForegroundColor Green
$filesToCopy = @(
    "BlueOpenServer.exe",
    "BlueOpenServer.dll",
    "BlueOpenServer.runtimeconfig.json",
    "BlueOpenServer.deps.json",
    "Microsoft.Windows.SDK.NET.dll",
    "WinRT.Runtime.dll"
)

foreach ($file in $filesToCopy) {
    $srcFile = Join-Path $srcServerDir $file
    if (Test-Path $srcFile) {
        Copy-Item -Path $srcFile -Destination $targetDir -Force
        Write-Host "  Copied $file"
    } else {
        Write-Error "Source file not found: $srcFile"
    }
}

# 3. Copy Credential Provider DLL to System32
$sys32Dir = "C:\Windows\System32"
$destDll = Join-Path $sys32Dir "BlueOpenProvider.dll"
$destDllOld = Join-Path $sys32Dir "BlueOpenProvider.dll.old"

Write-Host "Copying Credential Provider DLL to System32..." -ForegroundColor Green
if (Test-Path $destDllOld) {
    Remove-Item $destDllOld -Force -ErrorAction SilentlyContinue
}

try {
    if (Test-Path $destDll) {
        Move-Item -Path $destDll -Destination $destDllOld -Force -ErrorAction Stop
        Write-Host "  Renamed existing DLL to .old"
    }
    Copy-Item -Path $srcDll -Destination $destDll -Force -ErrorAction Stop
    Write-Host "  Successfully copied BlueOpenProvider.dll"
} catch {
    Write-Error "Failed to install DLL: $_"
}

# 4. Register Credential Provider in Registry
Write-Host "Registering Credential Provider in Registry..." -ForegroundColor Green
$guid = "{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}"

# COM CLSID registration
$clsidPath = "HKLM:\SOFTWARE\Classes\CLSID\$guid"
if (-not (Test-Path $clsidPath)) { New-Item -Path $clsidPath -Force | Out-Null }
Set-ItemProperty -Path $clsidPath -Name "(Default)" -Value "BlueOpen Credential Provider"

$inprocPath = Join-Path $clsidPath "InprocServer32"
if (-not (Test-Path $inprocPath)) { New-Item -Path $inprocPath -Force | Out-Null }
Set-ItemProperty -Path $inprocPath -Name "(Default)" -Value $destDll
Set-ItemProperty -Path $inprocPath -Name "ThreadingModel" -Value "Apartment"

# Windows Logon UI Credential Providers list registration
$cpPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
if (-not (Test-Path $cpPath)) { New-Item -Path $cpPath -Force | Out-Null }
Set-ItemProperty -Path $cpPath -Name "(Default)" -Value "BlueOpen Credential Provider"

# 5. Register Startup for auto-run
Write-Host "Registering BlueOpen Server in startup..." -ForegroundColor Green
$runPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
Set-ItemProperty -Path $runPath -Name "BlueOpenServer" -Value "`"$targetDir\BlueOpenServer.exe`""

# 6. Create Start Menu Shortcut
Write-Host "Creating Start Menu shortcut..." -ForegroundColor Green
$shortcutPath = "C:\ProgramData\Microsoft\Windows\Start Menu\Programs\BlueOpen Server.lnk"
try {
    $wshShell = New-Object -ComObject WScript.Shell
    $shortcut = $wshShell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = "$targetDir\BlueOpenServer.exe"
    $shortcut.WorkingDirectory = $targetDir
    $shortcut.Description = "BlueOpen Desktop Server"
    $shortcut.Save()
    Write-Host "  Shortcut created successfully."
} catch {
    Write-Warning "Failed to create Start Menu shortcut: $_"
}

Write-Host "`nInstallation Completed Successfully!" -ForegroundColor Cyan
Read-Host "Press Enter to exit"
