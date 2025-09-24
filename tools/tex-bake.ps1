param(
  [string]$In  = "$PSScriptRoot\..\res\textures",
  [string]$Out = "$PSScriptRoot\..\build\textures"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$texconv = Join-Path $PSScriptRoot "bin\texconv.exe"
if (!(Test-Path $texconv)) { throw "Place texconv.exe at tools\bin\texconv.exe (DirectXTex)." }

# LDR color to BC7 sRGB with full mipchain
& $texconv -y -srgb -f BC7_UNORM_SRGB -m 0 -o $Out -r $In

# Example: normals to BC5 (linear)
# & $texconv -y -f BC5_UNORM -m 0 -o "$Out\normals" -r "$In\normals"
Write-Host "Texture bake complete."
