; CRTBender telepito - Inno Setup 6 script.
;
; Vazlat: a program mar mukodik telepito nelkul is (egyetlen exe, hordozhato
; modban a config az exe melle kerul). Ez a script akkor jon jol, ha rendes
; Program Files-os telepitest szeretnel Start menu bejegyzessel.
;
; Forditas:  iscc installer\crtbender.iss
; Elvarja, hogy a build mar megtortent legyen:
;   cmake -B build -A x64 && cmake --build build --config Release

#define AppName        "CRTBender"
#define AppVersion     "1.1.0"
#define AppPublisher   "SubCoderHUN"
#define AppExeName     "CRTBender.exe"

[Setup]
AppId={{8C0E2B14-6D4A-4F3B-9E1C-2A7F5D6B3C90}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/SubCoderHUN/CRTBender
AppSupportURL=https://github.com/SubCoderHUN/CRTBender/issues
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
OutputBaseFilename=CRTBender-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Per-user telepites is eleg: a program nem ir sehova a HKCU-n kivul.
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; Windows 10 2004 kell a WDA_EXCLUDEFROMCAPTURE miatt.
MinVersion=10.0.19041

[Languages]
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "english";   MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                   DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\LICENSE";                     DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\{#AppExeName}"
Name: "{group}\{#AppName} eltavolitasa"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";      Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart";   Description: "Indulas a Windowszal"; GroupDescription: "Egyeb:"

[Registry]
; A program maga is kezeli ezt a kulcsot a talcamenubol; itt csak a telepitoben
; bepipalt allapotot allitjuk be induláskor.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "CRTBender"; ValueData: """{app}\{#AppExeName}"" --silent"; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; A beallitasok a felhasznaloi profilban maradnak, hogy ujratelepiteskor
; ne kelljen ujra kalibralni. Torles: %APPDATA%\CRTBender
Type: dirifempty; Name: "{app}"
