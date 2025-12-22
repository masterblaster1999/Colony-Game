# scripts/win/bootstrap.ps1
# Windows-only bootstrap: vcpkg + CMake configure/build

[CmdletBinding()]
param(
  [ValidateSet("Debug","Release","RelWithDebInfo")]
  [string]$Config = "Debug",

  [string]$BuildDir = "build",

  [string]$Triplet = "x64-windows"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
  $here = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $here "..\..")).Path
}

$repo = Resolve-RepoRoot
Write-Host "Repo root: $repo"

# Prefer an existing vcpkg (GitHub runners often have C:\vcpkg)
$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot -or -not (Test-Path $vcpkgRoot)) {
  if (Test-Path "C:\vcpkg") {
    $vcpkgRoot = "C:\vcpkg"
  } else {
    $vcpkgRoot = Join-Path $repo ".external\vcpkg"
    if (-not (Test-Path $vcpkgRoot)) {
      Write-Host "Cloning vcpkg into $vcpkgRoot..."
      git clone https://github.com/microsoft/vcpkg $vcpkgRoot
    }
    $bootstrap = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
    Write-Host "Bootstrapping vcpkg..."
    cmd /c "`"$bootstrap`" -disableMetrics"
  }
}

$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) {
  throw "vcpkg toolchain not found: $toolchain"
}

Write-Host "Using vcpkg: $vcpkgRoot"
Write-Host "Toolchain:   $toolchain"
Write-Host "Triplet:     $Triplet"

$buildPath = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

# Configure
cmake -S $repo -B $buildPath -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
  -DVCPKG_TARGET_TRIPLET="$Triplet" `
  -DCOLONY_BUILD_TESTS=ON `
  -DCOLONY_WERROR=ON

# Build
cmake --build $buildPath --config $Config --parallel

Write-Host "Done. Build output in: $buildPath"
