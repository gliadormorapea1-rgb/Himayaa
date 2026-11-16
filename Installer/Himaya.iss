#define MyAppName "Himaya Security"
#define MyAppVersion "0.7.0"
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
Name: "{commonappdata}\Himaya"; Permissions: admins-full system-full

[Files]
Source: "..\build\bin\Himaya.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\HimayaGuard.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\HimayaNativeHost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\BrowserExtension\*"; DestDir: "{app}\BrowserExtension"; Flags: ignoreversion recursesubdirs createallsubdirs

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
  HostPath: String;
  ManifestPath: String;
  ManifestText: String;
begin
  if CurStep = ssInstall then
  begin
    Exec(ExpandConstant('{sys}\\sc.exe'), 'stop HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(10000);
  end;
  if CurStep = ssPostInstall then
  begin
    HostPath := ExpandConstant('{app}\HimayaNativeHost.exe');
    StringChangeEx(HostPath, '\', '\\', True);
    ManifestPath := ExpandConstant('{app}\com.himaya.security.json');
    ManifestText := '{"name":"com.himaya.security","description":"Himaya Shorts and Reels Guard","path":"' + HostPath + '","type":"stdio","allowed_origins":["chrome-extension://mcghfimhokbfdgingelnojmgcneeppip/"]}';
    if not SaveStringToFile(ManifestPath, ManifestText, False) then
    begin
      MsgBox('تعذر إنشاء ملف ربط إضافة المتصفح. لم يتم إكمال تسجيل Native Messaging.', mbError, MB_OK);
      Abort;
    end;
    RegWriteStringValue(HKLM, 'SOFTWARE\Google\Chrome\NativeMessagingHosts\com.himaya.security', '', ManifestPath);
    RegWriteStringValue(HKLM, 'SOFTWARE\Microsoft\Edge\NativeMessagingHosts\com.himaya.security', '', ManifestPath);
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

function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if not Exec(ExpandConstant('{app}\\Himaya.exe'), '--check-permanent-strict-lock', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox('تعذر التحقق من حالة أقفال Strict. تم إلغاء إزالة البرنامج حفاظًا على الحماية.', mbCriticalError, MB_OK);
    exit;
  end;
  if ResultCode = 10 then
  begin
    MsgBox('لا يمكن إزالة Himaya لأن هناك تطبيقًا أو موقعًا مقفولًا بوضع Strict ومدة Long-term. هذا القفل لا ينتهي تلقائيًا.', mbCriticalError, MB_OK);
    exit;
  end;
  if ResultCode <> 0 then
  begin
    MsgBox('تعذر التأكد من حالة الحماية. لن تتم إزالة Himaya حتى يمكن التحقق منها بأمان.', mbCriticalError, MB_OK);
    exit;
  end;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec(ExpandConstant('{sys}\sc.exe'), 'stop HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(10000);
    Exec(ExpandConstant('{app}\Himaya.exe'), '--restore-dns', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec(ExpandConstant('{sys}\sc.exe'), 'delete HimayaGuard', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    RegDeleteKeyIncludingSubkeys(HKLM, 'SOFTWARE\Google\Chrome\NativeMessagingHosts\com.himaya.security');
    RegDeleteKeyIncludingSubkeys(HKLM, 'SOFTWARE\Microsoft\Edge\NativeMessagingHosts\com.himaya.security');
  end;
end;
