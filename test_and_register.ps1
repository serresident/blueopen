# Test and Register BlueOpen Credential Provider
param (
    [switch]$SkipTest = $false
)

$ErrorActionPreference = "Stop"

# Determine Workspace Directory reliably (even if commands are pasted into interactive console)
$workspaceDir = $PSScriptRoot
if (-not $workspaceDir -or (-not (Test-Path "$workspaceDir\BlueOpenProvider"))) {
    if (Test-Path ".\BlueOpenProvider") {
        $workspaceDir = (Get-Location).Path
    } elseif (Test-Path "c:\Users\erch\Мой диск\1 for ai\blueopen") {
        $workspaceDir = "c:\Users\erch\Мой диск\1 for ai\blueopen"
    }
}

$testerExe = "$workspaceDir\BlueOpenProvider\bin\x64\Release\TestProvider.exe"
$providerDll = "$workspaceDir\BlueOpenProvider\bin\x64\Release\BlueOpenProvider.dll"

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host " BlueOpen Credential Provider Test & Install " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "Workspace: $workspaceDir" -ForegroundColor Gray

# Verify provider DLL exists, build if missing
if (-not (Test-Path $providerDll)) {
    Write-Host "Building BlueOpenProvider.dll..." -ForegroundColor Yellow
    $msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $msbuild) {
        & $msbuild "$workspaceDir\BlueOpenProvider\BlueOpenProvider.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Build
    }
}

# 1. Run Automated Test Suite
if (-not $SkipTest) {
    if (Test-Path $testerExe) {
        Write-Host "`n--- Running Test Suite ---" -ForegroundColor Yellow
        & $testerExe
        if ($LASTEXITCODE -ne 0) {
            Write-Host "`nTest suite failed with exit code $LASTEXITCODE. Aborting installation." -ForegroundColor Red
            exit $LASTEXITCODE
        }
    } else {
        Write-Warning "TestProvider.exe not found at $testerExe. Skipping test suite."
    }
}

# 2. Check Administrator Privileges for System32 & HKLM registration
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "`n[NOTICE] Administrator rights are required to install BlueOpenProvider.dll to System32 and register it in Windows." -ForegroundColor Yellow
    Write-Host "Requesting elevation..." -ForegroundColor Cyan
    $scriptPath = if ($PSCommandPath) { $PSCommandPath } else { "$workspaceDir\test_and_register.ps1" }
    Start-Process powershell -Verb RunAs -WorkingDirectory "$workspaceDir" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`""
    exit
}

Write-Host "`n--- Registering BlueOpenProvider in Windows ---" -ForegroundColor Green

# Prepare ProgramData
$programDataDir = "C:\ProgramData\BlueOpen"
if (-not (Test-Path $programDataDir)) {
    New-Item -ItemType Directory -Path $programDataDir | Out-Null
}
icacls $programDataDir /grant "*S-1-5-32-545:(OI)(CI)M" /q | Out-Null

$userConfig = "$env:APPDATA\BlueOpen\config.json"
$targetConfig = "$programDataDir\config.json"
if ((Test-Path $userConfig) -and (-not (Test-Path $targetConfig))) {
    Copy-Item -Path $userConfig -Destination $targetConfig -Force
    icacls $targetConfig /grant "*S-1-5-32-545:M" /q | Out-Null
    Write-Host "Config copied to $targetConfig" -ForegroundColor Green
}

# Copy DLL to System32
$sys32Dir = "C:\Windows\System32"
$destDll = Join-Path $sys32Dir "BlueOpenProvider.dll"
$destDllOld = Join-Path $sys32Dir "BlueOpenProvider.dll.old"

if (Test-Path $destDllOld) {
    Remove-Item $destDllOld -Force -ErrorAction SilentlyContinue
}

try {
    if (Test-Path $destDll) {
        Move-Item -Path $destDll -Destination $destDllOld -Force -ErrorAction Stop
    }
    Copy-Item -Path $providerDll -Destination $destDll -Force -ErrorAction Stop
    Write-Host "[PASS] Copied BlueOpenProvider.dll to $destDll" -ForegroundColor Green
} catch {
    Write-Error "Failed to copy DLL to System32: $_"
}

# Register COM CLSID
$guid = "{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}"
$clsidPath = "HKLM:\SOFTWARE\Classes\CLSID\$guid"
if (-not (Test-Path $clsidPath)) { New-Item -Path $clsidPath -Force | Out-Null }
Set-ItemProperty -Path $clsidPath -Name "(Default)" -Value "BlueOpen Credential Provider"

$inprocPath = Join-Path $clsidPath "InprocServer32"
if (-not (Test-Path $inprocPath)) { New-Item -Path $inprocPath -Force | Out-Null }
Set-ItemProperty -Path $inprocPath -Name "(Default)" -Value $destDll
Set-ItemProperty -Path $inprocPath -Name "ThreadingModel" -Value "Apartment"

# Register in Credential Providers
$cpPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
if (-not (Test-Path $cpPath)) { New-Item -Path $cpPath -Force | Out-Null }
Set-ItemProperty -Path $cpPath -Name "(Default)" -Value "BlueOpen Credential Provider"

Write-Host "[PASS] Registered Credential Provider in Windows Registry." -ForegroundColor Green

Write-Host "`n=============================================" -ForegroundColor Cyan
Write-Host " Installation & Registration Complete!      " -ForegroundColor Green
Write-Host " BlueOpen is now active on the Lock Screen.  " -ForegroundColor Green
Write-Host " Test by pressing Win + L!                   " -ForegroundColor Yellow
Write-Host "=============================================" -ForegroundColor Cyan
