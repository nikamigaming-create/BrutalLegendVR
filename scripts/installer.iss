#ifndef SourceDir
  #error SourceDir must point to an audited runtime stage.
#endif
#ifndef ReleaseVersion
  #define ReleaseVersion "0.1.0-preview.4"
#endif
[Setup]
AppId={{C096980B-6910-4A02-B262-EE0D0A71AF1E}
AppName=Brütal Legend VR
AppVersion={#ReleaseVersion}
AppPublisher=Nikami
AppPublisherURL=https://github.com/nikamigaming-create/BrutalLegendVR
DefaultDirName={localappdata}\Programs\BrutalLegendVR
DefaultGroupName=Brütal Legend VR
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dark
WizardSizePercent=125
WizardImageFile=..\assets\installer\wizard.bmp
WizardImageStretch=yes
SetupIconFile=..\assets\installer\blvr.ico
UninstallDisplayIcon={app}\assets\installer\blvr.ico
OutputDir=..\release
OutputBaseFilename=BrutalLegendVR-{#ReleaseVersion}-Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
CloseApplicationsFilter=blvr_setup.exe,blvr_xr_host.exe
RestartApplications=no
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
UninstallDisplayName=Brütal Legend VR
VersionInfoVersion=0.1.0.4
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "controls.ini"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\controls.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
[Icons]
Name: "{group}\Brütal Legend VR"; Filename: "{app}\tools\blvr_setup.exe"; WorkingDir: "{app}"; IconFilename: "{app}\assets\installer\blvr.ico"
Name: "{group}\Play VR"; Filename: "{app}\tools\blvr_setup.exe"; Parameters: "--launch"; WorkingDir: "{app}"; IconFilename: "{app}\assets\installer\blvr.ico"
Name: "{group}\VR settings"; Filename: "{app}\tools\blvr_setup.exe"; Parameters: "--settings"; WorkingDir: "{app}"; IconFilename: "{app}\assets\installer\blvr.ico"
Name: "{group}\Uninstall Brütal Legend VR"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Brütal Legend VR"; Filename: "{app}\tools\blvr_setup.exe"; WorkingDir: "{app}"; IconFilename: "{app}\assets\installer\blvr.ico"; Tasks: desktopicon
[Run]
Filename: "{app}\tools\blvr_setup.exe"; Description: "Open Brütal Legend VR to prepare your game"; Flags: nowait postinstall skipifsilent
[Code]
function InitializeUninstall: Boolean;
var
  ResultCode: Integer;
  ErrorMessage: String;
begin
  Result := True;
  ErrorMessage := '';
  if FileExists(ExpandConstant('{app}\settings.json')) then begin
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\scripts\uninstall.ps1') + '"',
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      ErrorMessage := 'Could not restore the game files. Close the game and try uninstalling again.'
    else if ResultCode <> 0 then
      ErrorMessage := 'Game files could not be restored. Close Brütal Legend and use Uninstall VR before removing this app.';
  end;
  if ErrorMessage <> '' then begin
    Log(ErrorMessage);
    if not UninstallSilent then MsgBox(ErrorMessage, mbError, MB_OK);
    Result := False;
  end;
end;
