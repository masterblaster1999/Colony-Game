; Minimal installer for Colony-Game (Windows)
; Place this file in: <repo>\installer\ColonyGame.iss

#define MyScriptDir __DIR__
#include "paths.iss"   ; centralizes app metadata and build/output paths

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
WizardStyle=modern
; 64-bit install (remove these two lines if you actually ship 32-bit builds)
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

; Where the compiled setup EXE is written
OutputDir={#OutputDir}
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Setup

; Optional: use a custom icon for the installer/uninstaller if set in paths.iss
#if SetupIcon != ""
SetupIconFile={#SetupIcon}
#endif

Compression=lzma2
SolidCompression=yes

[Files]
; Main EXE + DLLs
Source: "{#BinDir}\{#MainExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\*.dll";          DestDir: "{app}"; Flags: ignoreversion recursesubdirs

; Game data
Source: "{#AssetsDir}\*";  DestDir: "{app}\assets";  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ResDir}\*";     DestDir: "{app}\res";     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ShadersDir}\*"; DestDir: "{app}\shaders"; Flags: ignoreversion recursesubdirs createallsubdirs

; Optional: ship PDBs for dev builds
; Source: "{#BinDir}\*.pdb"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Start Menu shortcut + optional desktop shortcut
Name: "{group}\{#MyAppName}";      Filename: "{app}\{#MainExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MainExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
; Offer to launch after install
Filename: "{app}\{#MainExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
