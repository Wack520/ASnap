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
DisableDirPage=no
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

[Messages]
WelcomeLabel1=欢迎安装 [name]
WelcomeLabel2=[name/ver] 将安装到您的电脑。%n%n它适合快速截图、贴边提问和继续追问。安装前建议先关闭其他正在运行的程序。
SelectDirLabel3=安装程序会将 [name] 安装到下面的文件夹中。
SelectTasksLabel2=选择您希望安装程序额外执行的项目，然后点击“下一步”。
ReadyLabel1=安装程序已准备就绪，可以开始安装 [name]。
ReadyLabel2a=点击“安装”继续。如果您想返回修改设置，请点击“上一步”。
FinishedHeadingLabel=[name] 安装完成
FinishedLabel=ASnap 已安装到这台电脑。您现在可以直接启动它开始使用。

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
