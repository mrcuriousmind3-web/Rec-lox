#define AppName "Rec-lox"
#define AppVersion "0.1.0"
#define AppPublisher "Rec-lox Open Source"
#define AppExeName "Rec-lox.exe"

[Setup]
AppId={{C1B4D884-32B0-4A82-A06F-94EA0CBE24D1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/mrcuriousmind3-web/Rec-lox
AppSupportURL=https://github.com/mrcuriousmind3-web/Rec-lox/issues
DefaultDirName={autopf}\Rec-lox
DefaultGroupName=Rec-lox
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\dist
OutputBaseFilename=Rec-lox-Setup
SetupIconFile=..\assets\rec-lox.ico
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\Release\Rec-lox.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Rec-lox"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\Rec-lox"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch Rec-lox"; Flags: postinstall nowait skipifsilent
