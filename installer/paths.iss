; Centralized app metadata and paths for Colony-Game
; Place this file in: <repo>\installer\paths.iss
; NOTE: This file is included by ColonyGame.iss at compile time.

; --- App metadata (edit as needed) ---
#define MyAppId "{{52D9F0D8-1E5A-4F20-9A0D-123456789ABC}}"   ; TODO: generate once and keep constant
#define MyAppName "Colony Game"
#define MyAppPublisher "masterblaster1999"
#define MyAppVersion "0.1.0"
#define MyAppURL "https://github.com/masterblaster1999/Colony-Game"
#define MainExeName "WinLauncher.exe"    ; Change if your target exe has a different name

; --- Derive repo-relative paths (assumes this file lives in <repo>\installer\) ---
#define InstallerDir  AddBackslash(__DIR__)
#define ProjectRoot   ExtractFileDir(InstallerDir)
#define ProjectRootBS AddBackslash(ProjectRoot)

; If CMake outputs directly to build\RelWithDebInfo\:
#define BinDir ProjectRootBS + "build\\RelWithDebInfo"

; If your CMake puts binaries under build\bin\RelWithDebInfo\, use this instead:
;#define BinDir ProjectRootBS + "build\\bin\\RelWithDebInfo"

#define AssetsDir   ProjectRootBS + "assets"
#define ResDir      ProjectRootBS + "res"
#define ShadersDir  ProjectRootBS + "shaders"

; Where to write the compiled installer exe
#define OutputDir   ProjectRootBS + "dist"
#expr ForceDirectories(OutputDir)

; Optional custom icon for the installer/uninstaller (.ico). Leave empty to use default.
#define SetupIcon  ""
; Example: #define SetupIcon  ProjectRootBS + "res\\icons\\app.ico"

; --- Build-time sanity check (fail early if exe missing) ---
#if FileExists(BinDir + "\\" + MainExeName) == 0
  #pragma error "Main EXE not found at: " + BinDir + "\\" + MainExeName + "  (build first or adjust BinDir/MainExeName in paths.iss)"
#endif
