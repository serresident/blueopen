$workspaceDir = $PSScriptRoot
if (-not $workspaceDir -or (-not (Test-Path "$workspaceDir\BlueOpenProvider"))) {
    if (Test-Path ".\BlueOpenProvider") {
        $workspaceDir = (Get-Location).Path
    } elseif (Test-Path "c:\Users\erch\Мой диск\1 for ai\blueopen") {
        $workspaceDir = "c:\Users\erch\Мой диск\1 for ai\blueopen"
    }
}

# Ensure running as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "This script must be run as Administrator! Requesting elevation..."
    $scriptPath = if ($PSCommandPath) { $PSCommandPath } else { "$workspaceDir\install_blueopen.ps1" }
    Start-Process powershell -Verb RunAs -WorkingDirectory "$workspaceDir" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`""
    exit
}

$targetDir = "C:\Program Files\BlueOpen"
$srcServerDir = "$workspaceDir\BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0"
$srcDll = "$workspaceDir\BlueOpenProvider\bin\x64\Release\BlueOpenProvider.dll"

Write-Host "--- BlueOpen Installer ---" -ForegroundColor Cyan

# 0. Build Provider DLL if missing
if (-not (Test-Path $srcDll)) {
    Write-Host "BlueOpenProvider.dll not found. Building now..." -ForegroundColor Yellow
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = $null
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
    }
    if (-not $msbuild -or -not (Test-Path $msbuild)) {
        $msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    }

    if (Test-Path $msbuild) {
        & $msbuild "$workspaceDir\BlueOpenProvider\BlueOpenProvider.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Build
    }
}

# 1. Create Target Directory
if (-not (Test-Path $targetDir)) {
    Write-Host "Creating target folder: $targetDir"
    New-Item -ItemType Directory -Path $targetDir | Out-Null
}

# Create ProgramData directory for shared config and grant modify permissions to Users (SID: S-1-5-32-545)
$programDataDir = "C:\ProgramData\BlueOpen"
if (-not (Test-Path $programDataDir)) {
    Write-Host "Creating shared ProgramData folder: $programDataDir"
    New-Item -ItemType Directory -Path $programDataDir | Out-Null
}
icacls $programDataDir /grant "*S-1-5-32-545:(OI)(CI)M" /q | Out-Null

# Copy user config if exists in AppData
$userConfig = "$env:APPDATA\BlueOpen\config.json"
$targetConfig = "$programDataDir\config.json"
if ((Test-Path $userConfig) -and (-not (Test-Path $targetConfig))) {
    Copy-Item -Path $userConfig -Destination $targetConfig -Force
    icacls $targetConfig /grant "*S-1-5-32-545:M" /q | Out-Null
    Write-Host "Migrated user config to $targetConfig" -ForegroundColor Green
}

# 2. Close running server before copying files
Get-Process BlueOpenServer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# 3. Copy WPF Server Application files (if compiled)
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
    }
}

# 4. Copy Credential Provider DLL to System32
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

# 5. Register Credential Provider in Registry
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

# 6. Register Startup for auto-run
Write-Host "Registering BlueOpen Server in startup..." -ForegroundColor Green
$runPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
$exePath = Join-Path $targetDir "BlueOpenServer.exe"
if (Test-Path $exePath) {
    Set-ItemProperty -Path $runPath -Name "BlueOpenServer" -Value "`"$exePath`""
}

# 7. Create Start Menu Shortcut
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

# Restart BlueOpenServer if exe exists
if (Test-Path $exePath) {
    Write-Host "Starting BlueOpen Server..." -ForegroundColor Green
    Start-Process -FilePath $exePath
}

Write-Host "`nInstallation Completed Successfully!" -ForegroundColor Cyan
