# tools\fix-presets.ps1
$ErrorActionPreference = "Stop"
$path   = "CMakePresets.json"
$backup = "CMakePresets.json.bak"

if (-not (Test-Path $path)) {
  Write-Error "CMakePresets.json not found in the current directory."
}

Copy-Item $path $backup -Force
$json = Get-Content $path -Raw | ConvertFrom-Json

# Helper to rename a property on a PSCustomObject (preserving the value)
function Rename-JsonProperty {
  param(
    [Parameter(Mandatory=$true)] [object]$obj,
    [Parameter(Mandatory=$true)] [string]$oldName,
    [Parameter(Mandatory=$true)] [string]$newName
  )
  if ($obj.PSObject.Properties.Name -contains $oldName) {
    $val = $obj.$oldName
    # Only set if not already present (so we don't overwrite an explicit canonical value)
    if (-not ($obj.PSObject.Properties.Name -contains $newName)) {
      $obj | Add-Member -NotePropertyName $newName -NotePropertyValue $val
    }
    $null = $obj.PSObject.Properties.Remove($oldName)
  }
}

# Patch every configure preset's cacheVariables
foreach ($p in $json.configurePresets) {
  if ($null -eq $p.cacheVariables) { continue }
  $cv = $p.cacheVariables

  # Rename old → canonical
  Rename-JsonProperty -obj $cv -oldName "CG_ENABLE_PCH"   -newName "COLONY_USE_PCH"
  Rename-JsonProperty -obj $cv -oldName "CG_ENABLE_UNITY" -newName "COLONY_UNITY_BUILD"

  # Force the real project standard
  if ($cv.PSObject.Properties.Name -contains "CMAKE_CXX_STANDARD") {
    $cv.CMAKE_CXX_STANDARD = "23"
  } else {
    $cv | Add-Member -NotePropertyName "CMAKE_CXX_STANDARD" -NotePropertyValue "23"
  }
}

# Write back (Depth≥10 to cover nested preset structures)
$json | ConvertTo-Json -Depth 10 | Set-Content $path -NoNewline -Encoding UTF8
Write-Host "Updated $path. Backup saved to $backup."
