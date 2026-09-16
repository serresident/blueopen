$ErrorActionPreference = "Stop"
$ServerProject = ".\BlueOpenServer\BlueOpenServer.csproj"
$SetupProject = ".\BlueOpenSetup\BlueOpenSetup.csproj"
$ProviderProject = ".\BlueOpenProvider\BlueOpenProvider.vcxproj"
$PublishDir = ".\ReleaseBuild"

if (-not (Test-Path $PublishDir)) {
    New-Item -ItemType Directory -Path $PublishDir | Out-Null
}

Write-Host "1. Building Windows Credential Provider (BlueOpenProvider.dll)..." -ForegroundColor Cyan
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
}
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    $msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
}

if (Test-Path $msbuild) {
    & $msbuild $ProviderProject /p:Configuration=Release /p:Platform=x64 /t:Rebuild
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BlueOpenProvider build failed." -ForegroundColor Red
        exit 1
    }
    Write-Host "Successfully built BlueOpenProvider.dll" -ForegroundColor Green
    Copy-Item -Path ".\BlueOpenProvider\bin\x64\Release\BlueOpenProvider.dll" -Destination "$PublishDir\BlueOpenProvider.dll" -Force
} else {
    Write-Warning "MSBuild.exe not found! Skipping BlueOpenProvider compilation."
}

Write-Host "`n2. Building BlueOpenServer..." -ForegroundColor Cyan

# Publish Server as single file
dotnet publish $ServerProject -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o ".\BlueOpenSetup\Payload"

if ($LASTEXITCODE -ne 0) {
    Write-Host "Server build failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n3. Building BlueOpenSetup Installer..." -ForegroundColor Cyan

# Publish Setup as single file
dotnet publish $SetupProject -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o $PublishDir

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nSuccessfully built Installer!" -ForegroundColor Green
    Write-Host "Your installer is at: $(Resolve-Path $PublishDir)\BlueOpenSetup.exe" -ForegroundColor Yellow
} else {
    Write-Host "`nInstaller build failed." -ForegroundColor Red
}

Write-Host "`n4. Copying Android APK to ReleaseBuild folder for GitHub..." -ForegroundColor Cyan
$ApkSource = ".\BlueOpenClient\app\build\outputs\apk\release\app-release.apk"
$ApkTarget = ".\ReleaseBuild\BlueOpenClient.apk"

if (Test-Path $ApkSource) {
    Copy-Item -Path $ApkSource -Destination $ApkTarget -Force
    Write-Host "Successfully copied Android APK to ReleaseBuild folder!" -ForegroundColor Green
} else {
    Write-Host "Release APK not found at $ApkSource. Skipping APK copy." -ForegroundColor Yellow
}
