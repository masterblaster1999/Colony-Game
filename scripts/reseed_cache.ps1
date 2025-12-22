param(
  [string]$SourceDir = "$PSScriptRoot\..",
  [string]$BuildDir  = "$PSScriptRoot\..\build",
  [string]$Preset    = "windows-release"  # or pass -Preset <name>
)

$ErrorActionPreference = "Stop"

# Step 1: ensure a configure has run enough to evaluate the fingerprint
if (!(Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

# A tiny helper CMake to print a fresh fingerprint without touching the cache
$tempProbe = Join-Path $BuildDir ".fingerprint-probe"
@"
cmake_minimum_required(VERSION 3.25)
list(PREPEND CMAKE_MODULE_PATH "$($SourceDir -replace '\\','/')/cmake")
include(ConfigFingerprint)
colony_compute_fingerprint(FP)
message(STATUS "COLONY_FRESH_FP=${FP}")
"@ | Set-Content -Encoding ASCII $tempProbe.cmake

# Run a probe (generator preset preferred, but fallback is OK)
$probe = & cmake -S "$SourceDir" -B "$BuildDir" --preset $Preset 2>$null
$cmout = & cmake -P $tempProbe.cmake -D CMAKE_SOURCE_DIR="$SourceDir" -D CMAKE_BINARY_DIR="$BuildDir"
Remove-Item "$tempProbe.cmake" -Force

# Parse fresh fingerprint from output
$fresh = ($cmout | Select-String "COLONY_FRESH_FP=").Line.Split("=")[1].Trim()

# Read cached fingerprint if present
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
$cached = $null
if (Test-Path $cacheFile) {
  $cached = (Get-Content $cacheFile | Select-String "^COLONY_CONFIG_FINGERPRINT:STRING=").Line.Split("=")[2]
}

$changed = ($cached -ne $fresh)

if ($changed) {
  Write-Host "🔄 Config changed → purging stale cache"
  Remove-Item -Recurse -Force (Join-Path $BuildDir "*") -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Final configure & (optional) build using the preset
& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "Configure failed." }

# Uncomment to build in one go:
# & cmake --build --preset $Preset
