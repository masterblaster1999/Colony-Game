; installer/colony.iss
[Setup]
AppName=Colony Game
AppVersion=1.0.0
DefaultDirName={autopf}\Colony Game

[Files]
Source: "..\bin\ColonyGame.exe"; DestDir: "{app}"
Source: "..\res\*"; DestDir: "{app}\res"; Flags: recursesubdirs

[Icons]
Name: "{group}\Colony Game"; Filename: "{app}\ColonyGame.exe"; AppUserModelID: "ColonyGame.Colony"

[Run]
Filename: "{app}\ColonyGame.exe"; Description: "Launch now"; Flags: nowait postinstall skipifsilent
