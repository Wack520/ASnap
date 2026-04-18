#ifndef AppVersion
  #define AppVersion "local"
#endif

#ifndef SourceDir
  #error "SourceDir define is required."
#endif

#ifndef OutputDir
  #error "OutputDir define is required."
#endif

#ifndef OutputBaseFilename
  #define OutputBaseFilename "ASnap-Setup"
#endif

#ifndef RepoRoot
  #define RepoRoot "..\.."
#endif

#define MyAppName "ASnap"
#define MyAppPublisher "Wack520"
#define MyAppURL "https://github.com/Wack520/ASnap"
#define MyAppExeName "ASnap.exe"
#define MyAppAssocName MyAppName + " Installer"

[Setup]
AppId={{7D0AF2C8-11A8-4DE2-8A9E-4B5B2FBA0F35}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#RepoRoot}\native\assets\branding\asnap-icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousTasks=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional tasks:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
