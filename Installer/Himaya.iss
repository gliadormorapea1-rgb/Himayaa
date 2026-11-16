#define MyAppName "Himaya Security"
#define MyAppVersion "0.4.0"
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
CloseApplications=yes

[Dirs]
Name: "{commonappdata}\Himaya"; Permissions: users-modify

[Files]
Source: "..\build\bin\Himaya.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\HimayaGuard.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Himaya Security"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Himaya Security"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Himaya Security"; Flags: postinstall nowait skipifsilent

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Params: String;
begin
  if CurStep = ssPostInstall then
  begin
    Params := 'create HimayaGuard binPath= "' + ExpandConstant('{app}\HimayaGuard.exe') + '" start= auto DisplayName= "Himaya Protection Service"';
    Exec(ExpandConstant('{sys}\sc.exe'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Params := 'config HimayaGuard start= auto binPath= "' + ExpandConstant('{app}\HimayaGuard.exe') + '"';
    if (not Exec(ExpandConstant('{sys}\sc.exe'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    begin
      MsgBox('تعذر تسجيل خدمة الحماية HimayaGuard. لن يعمل حظر التطبيقات والمجلدات حتى إصلاح تثبيت الخدمة.', mbError, MB_OK);
      Abort;
    end;
    if (not Exec(ExpandConstant('{sys}\sc.exe'), 'start HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or ((ResultCode <> 0) and (ResultCode <> 1056)) then
    begin
      MsgBox('تم تثبيت الملفات، لكن تعذر تشغيل خدمة الحماية. افتح Services وتحقق من HimayaGuard.', mbError, MB_OK);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec(ExpandConstant('{sys}\sc.exe'), 'stop HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec(ExpandConstant('{sys}\sc.exe'), 'delete HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
