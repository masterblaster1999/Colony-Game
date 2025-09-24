param(
  [string]$Watch = "$PSScriptRoot\..\res\shaders",
  [string]$BuildScript = "$PSScriptRoot\shader-build.ps1"
)
$ErrorActionPreference = "Stop"
Write-Host "Watching $Watch for HLSL changes..."

$fsw = New-Object System.IO.FileSystemWatcher $Watch, "*.hlsl"
$fsw.IncludeSubdirectories = $true
$fsw.EnableRaisingEvents = $true

$action = {
  Start-Sleep -Milliseconds 120
  try { & $using:BuildScript } catch { Write-Warning $_ }
}

Register-ObjectEvent $fsw Changed -Action $action | Out-Null
Register-ObjectEvent $fsw Created -Action $action | Out-Null
Register-ObjectEvent $fsw Renamed -Action $action | Out-Null

while ($true) { Start-Sleep -Seconds 1 }
