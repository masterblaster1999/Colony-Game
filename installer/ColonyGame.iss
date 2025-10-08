; Minimal installer for Colony-Game (Windows)
; Place this file in: <repo>\installer\ColonyGame.iss

#define MyScriptDir __DIR__
#include "paths.iss"   ; centralizes app metadata and build/output paths

; ---- Agility (DirectX 12) packaging checks ---------------------------------
; Expect the DirectX 12 Agility SDK redistributable staged at:
;   {#BinDir}\D3D12\D3D12Core.dll
; Docs: https://aka.ms/directx12agility
; ISPP built-ins used below: AddBackslash, DirExists, FileExists. :contentReference[oaicite:3]{index=3}
#define AgilityDir AddBackslash(BinDir) + "D3D12"
#if !DirExists(AgilityDir)
  #expr Error("Missing Agility folder: " + AgilityDir + " (place Agility redist DLLs here, incl. D3D12Core.dll).")
#endif
#if !FileExists(AddBackslash(AgilityDir) + "D3D12Core.dll")
  #expr Error("D3D12 folder found but D3D12Core.dll is missing at: " + AgilityDir + ".")
#endif
; ---------------------------------------------------------------------------

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

; --- Code signing (Inno Setup) ---
; We use a named Sign Tool "signtool". Its actual command line is supplied by CI
; via the iscc.exe /S switch. SignedUninstaller ensures the uninstaller is signed. :contentReference[oaicite:4]{index=4}
SignTool=signtool
SignedUninstaller=yes
SignToolRetryCount=3
SignToolMinimumTimeBetween=5000
SignToolRunMinimized=yes

[Files]
; Main EXE + DLLs
Source: "{#BinDir}\{#MainExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\*.dll";          DestDir: "{app}"; Flags: ignoreversion recursesubdirs

; *** DirectX 12 Agility SDK redist ***
; Recursively copy the entire D3D12\ tree into {app}\D3D12 and create empty
; subfolders if present. (See 'recursesubdirs' and 'createallsubdirs'.) :contentReference[oaicite:5]{index=5}
Source: "{#BinDir}\D3D12\*"; DestDir: "{app}\D3D12"; Flags: ignoreversion recursesubdirs createallsubdirs

; Game data
Source: "{#AssetsDir}\*";  DestDir: "{app}\assets";  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ResDir}\*";     DestDir: "{app}\res";     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ShadersDir}\*"; DestDir: "{app}\shaders"; Flags: ignoreversion recursesubdirs createallsubdirs

; Optional: ship PDBs for dev builds
; Source: "{#BinDir}\*.pdb"; DestDir: "{app}"; Flags: ignoreversion

; Optional: ask the compiler to sign installed binaries if not already signed.
; Requires [Setup] SignTool above. Uncomment and adapt if you want Inno to sign
; the EXEs/DLLs it installs (instead of signing them earlier in the pipeline):
; Source: "{#BinDir}\*.exe"; DestDir: "{app}"; Flags: ignoreversion signonce recursesubdirs
; Source: "{#BinDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion signonce recursesubdirs

[Icons]
; Start Menu shortcut + optional desktop shortcut
Name: "{group}\{#MyAppName}";       Filename: "{app}\{#MainExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MainExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
; Offer to launch after install
Filename: "{app}\{#MainExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Not strictly required, but makes intent explicit. Directories are created automatically when installing files. :contentReference[oaicite:6]{index=6}
Type: dirifempty; Name: "{app}\D3D12"
