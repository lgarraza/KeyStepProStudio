; KeyStepPro Studio installer (Inno Setup 6). Built by installer/build-installer.ps1, which passes AppVersion and BuildDir:
;   ISCC.exe /DAppVersion=0.2.2 /DBuildDir=..\build installer\KeyStepProStudio.iss
; Installs per user by default (no admin prompt); the dialog offers "all users" too.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

#define AppName "KeyStepPro Studio"
#define AppExe "KeyStepPro Studio.exe"

[Setup]
AppId={{6F1E2C1B-5A4D-4C7E-9B2A-3D8E7F1A2B4C}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=KeyStepPro Studio
AppPublisherURL=https://github.com/lgarraza/KeyStepProStudio
AppSupportURL=https://github.com/lgarraza/KeyStepProStudio/issues
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
SetupIconFile=..\App\Resources\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=KeyStepProStudio-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; the app and its plugin scanner (the app looks for KspScanner.exe next to itself)
Source: "{#BuildDir}\KeyStepProStudio_artefacts\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\KspScanner_artefacts\Release\KspScanner.exe"; DestDir: "{app}"; Flags: ignoreversion
; command-line tools
Source: "{#BuildDir}\KspProbe_artefacts\Release\KspProbe.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#BuildDir}\KspBackup_artefacts\Release\KspBackup.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#BuildDir}\KspRestore_artefacts\Release\KspRestore.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#BuildDir}\KspGen_artefacts\Release\KspGen.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#BuildDir}\KspHostCheck_artefacts\Release\KspHostCheck.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
; documents
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\KeyStepPro-Studio-Manual.pdf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\User manual (PDF)"; Filename: "{app}\KeyStepPro-Studio-Manual.pdf"
Name: "{group}\Command-line tools"; Filename: "{cmd}"; Parameters: "/k cd /d ""{app}\tools"""; WorkingDir: "{app}\tools"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; The library, backups, recordings and plugin list in Documents\KeyStepPro Studio are the user's data: the uninstaller leaves them.
