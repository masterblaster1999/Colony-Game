[Setup]
AppName=Colony Game
AppVersion={#MyAppVersion}
DefaultDirName={pf}\Colony Game

[Files]
Source: "..\build\bin\Release\WinLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\shaders\*"; DestDir: "{app}\shaders"; Flags: recursesubdirs createallsubdirs
Source: "..\res\*"; DestDir: "{app}\res"; Flags: recursesubdirs createallsubdirs
