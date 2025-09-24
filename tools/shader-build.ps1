param(
  [string]$In  = "$PSScriptRoot\..\res\shaders",
  [string]$Out = "$PSScriptRoot\..\build\shaders",
  [switch]$Debug
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# Try common SDK locations for fxc.exe
$fxcCandidates = @(
  "${env:WindowsSdkDir}bin\x64\fxc.exe",
  "${env:ProgramFiles(x86)}\Windows Kits\10\bin\x64\fxc.exe",
  "${env:ProgramFiles(x86)}\Windows Kits\10\bin\${env:WindowsSDKVersion}\x64\fxc.exe"
) | Where-Object { Test-Path $_ }
if (-not $fxcCandidates) { throw "fxc.exe not found. Open a VS Developer Prompt or install Windows 10/11 SDK." }
$fxc = $fxcCandidates[0]

$opt  = if ($Debug) { "/Zi","/Od" } else { "/O3" }
$defs = @()  # add /D FOO=1 if needed

Get-ChildItem -Path $In -Filter *.hlsl -Recurse | ForEach-Object {
  $src = $_.FullName
  $txt = Get-Content -LiteralPath $src -Raw
  $jobs = @()

  # crude entry detection by convention
  if ($txt -match "\bPSMain\s*\(") { 
    $out = Join-Path $Out ("{0}PS.cso" -f [IO.Path]::GetFileNameWithoutExtension($_.Name))
    $args = @("/T","ps_5_0","/E","PSMain","/Fo",$out) + $opt + $defs + @($src)
    & $fxc $args | Write-Host
  }
  if ($txt -match "\bVSMain\s*\(") {
    $out = Join-Path $Out ("{0}VS.cso" -f [IO.Path]::GetFileNameWithoutExtension($_.Name))
    $args = @("/T","vs_5_0","/E","VSMain","/Fo",$out) + $opt + $defs + @($src)
    & $fxc $args | Write-Host
  }
  if ($txt -match "\bCSMain\s*\(") {
    $out = Join-Path $Out ("{0}CS.cso" -f [IO.Path]::GetFileNameWithoutExtension($_.Name))
    $args = @("/T","cs_5_0","/E","CSMain","/Fo",$out) + $opt + $defs + @($src)
    & $fxc $args | Write-Host
  }
}
Write-Host "HLSL build complete."
