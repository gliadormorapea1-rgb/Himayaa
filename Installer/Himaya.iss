#define MyAppName "Himaya Security"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Himaya Project"
#define MyAppExeName "Himaya.exe"

[Setup]
AppId={{B16D43A2-57F1-4D6B-9C1B-74F0B3D24C01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Himaya
DefaultGroupName=Himaya Security
OutputDir=Output
OutputBaseFilename=Himaya-Setup
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "..\build\bin\Himaya.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Himaya Security"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Himaya Security"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Himaya Security"; Flags: postinstall nowait skipifsilent
