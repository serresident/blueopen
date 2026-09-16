[Setup]
AppName=BlueOpen
AppVersion=1.0
DefaultDirName={commonpf}\BlueOpen
DefaultGroupName=BlueOpen
UninstallDisplayIcon={app}\BlueOpenServer.exe
Compression=lzma2
SolidCompression=yes
OutputDir=.\
OutputBaseFilename=BlueOpenSetup
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Files]
; Copy the WPF Server app files
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\BlueOpenServer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\BlueOpenServer.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\BlueOpenServer.runtimeconfig.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\BlueOpenServer.deps.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\Microsoft.Windows.SDK.NET.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "BlueOpenServer\bin\Release\net8.0-windows10.0.19041.0\WinRT.Runtime.dll"; DestDir: "{app}"; Flags: ignoreversion

; Copy the Credential Provider DLL to System32
Source: "BlueOpenProvider\bin\x64\Release\BlueOpenProvider.dll"; DestDir: "{sys}"; Flags: ignoreversion restartreplace

[Dirs]
Name: "{commonappdata}\BlueOpen"; Permissions: users-modify

[Registry]
; Register COM object CLSID for the Credential Provider
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}"; ValueType: string; ValueName: ""; ValueData: "BlueOpen Credential Provider"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{sys}\BlueOpenProvider.dll"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletekey

; Register as Windows Credential Provider
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{{8a3b8d4f-3c8b-4a5f-9e8a-0c2d3b4a5f6e}"; ValueType: string; ValueName: ""; ValueData: "BlueOpen Credential Provider"; Flags: uninsdeletekey

; Run on Windows startup for all users
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "BlueOpenServer"; ValueData: """{app}\BlueOpenServer.exe"""; Flags: uninsdeletevalue

[Icons]
Name: "{group}\BlueOpen Server"; Filename: "{app}\BlueOpenServer.exe"
Name: "{commonstartup}\BlueOpen Server"; Filename: "{app}\BlueOpenServer.exe"

[Run]
Filename: "{app}\BlueOpenServer.exe"; Description: "Launch BlueOpen Server"; Flags: postinstall nowait shellexec
