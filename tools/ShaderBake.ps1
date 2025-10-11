param(
  [string]$In = "assets/shaders",
  [string]$Out = "assets/shaders/compiled",
  [string]$Profile = "ps_6_0"  # or vs_6_0, cs_6_0 etc
)

New-Item -ItemType Directory -Force -Path $Out | Out-Null

Get-ChildItem $In -Filter *.hlsl -Recurse | ForEach-Object {
  $inFile = $_.FullName
  $outFile = Join-Path $Out ($_.BaseName + ".cso")
  & "$PSScriptRoot\..\vcpkg\installed\x64-windows\tools\dxc\dxc.exe" `
      -T $Profile -E main -Fo $outFile -Qstrip_debug -Qstrip_reflect $inFile
}
