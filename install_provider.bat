@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo  BlueOpen Credential Provider - Installer
echo ========================================================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_and_register.ps1"

echo.
pause
