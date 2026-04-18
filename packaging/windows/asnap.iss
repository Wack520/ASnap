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
#define MyAppVersionedName MyAppName + " " + AppVersion
#define MyAppPublisher "Wack520"
#define MyAppURL "https://github.com/Wack520/ASnap"
#define MyAppExeName "ASnap.exe"

[Setup]
AppId={{7D0AF2C8-11A8-4DE2-8A9E-4B5B2FBA0F35}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppVersionedName}
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
UninstallDisplayName={#MyAppName}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=ASnap 安装程序
VersionInfoProductName={#MyAppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
WizardImageFile={#RepoRoot}\packaging\windows\assets\wizard-side.bmp
WizardSmallImageFile={#RepoRoot}\packaging\windows\assets\wizard-small.bmp
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
ShowLanguageDialog=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "{#RepoRoot}\packaging\windows\languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[CustomMessages]
LaunchProgram=安装完成后立即启动 ASnap

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent
