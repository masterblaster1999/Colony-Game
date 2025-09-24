param(
  [string]$Exe = "$PSScriptRoot\..\build\bin\Release\WinLauncher.exe",
  [string]$Pix = "C:\Program Files\Microsoft PIX\pixtool.exe"
)
if (!(Test-Path $Pix)) { throw "Install PIX for Windows and update \$Pix." }
& "$Pix" launch -target:$Exe -captureafter:2 -captures:2
