# scripts/win/bootstrap.ps1
# Windows-only bootstrap for Colony-Game:
#  1) Finds/bootstraps vcpkg
#  2) Runs "vcpkg install" in manifest mode and writes build\vcpkg-manifest-install.log
#  3) Configures + builds with CMake (MSVC / VS2022 generator by default)

[CmdletBinding()]
param(
  [string]$BuildDir = "",
  [ValidateSet("Debug","Release","RelWithDebInfo","MinSizeRel")]
  [string]$Config = "Debug",
  [string]$Triplet = "x64-windows",
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Architecture = "x64",
  [switch]$NoBuild,
  [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
  $here = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $here "..\..")).Path
}

function Ensure-Dir([string]$Path) {
  if (-not (Test-Path $Path)) { New-Item -ItemType Directory -Force -Path $Path | Out-Null }
}

function Find-VcpkgRoot([string]$RepoRoot) {
  # Preferred: VCPKG_ROOT env var
  if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
    return (Resolve-Path $env:VCPKG_ROOT).Path
  }

  # Common CI/local location
  if (Test-Path "C:\vcpkg\vcpkg.exe") {
    return "C:\vcpkg"
  }

  # Fallback: clone into repo
  $thirdParty = Join-Path $RepoRoot "external"
  $vcpkg = Join-Path $thirdParty "vcpkg"
  if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    Ensure-Dir $thirdParty
    Write-Host "Cloning vcpkg into $vcpkg ..."
    git clone https://github.com/microsoft/vcpkg.git $vcpkg
  }

  # Bootstrap (idempotent)
  $bootstrap = Join-Path $vcpkg "bootstrap-vcpkg.bat"
  if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    Write-Host "Bootstrapping vcpkg..."
    cmd /c "`"$bootstrap`" -disableMetrics"
  }

  return $vcpkg
}

function Default-Caching-For-CI {
  # These env vars are standard ways to control vcpkg behavior. :contentReference[oaicite:1]{index=1}
  # Enable in CI if not already set by workflow.
  if ($env:CI) {
    # Binary caching speeds up and stabilizes CI builds. :contentReference[oaicite:2]{index=2}
    if (-not $env:VCPKG_BINARY_SOURCES) {
      # GitHub Actions cache backend
      $env:VCPKG_BINARY_SOURCES = "clear;x-gha,readwrite"
    }
    # Asset caching reduces flaky external downloads (e.g., source archives). :contentReference[oaicite:3]{index=3}
    if (-not $env:X_VCPKG_ASSET_SOURCES) {
      $env:X_VCPKG_ASSET_SOURCES = "clear;x-gha,readwrite"
    }
  }
}

function Run-VcpkgInstall(
  [string]$VcpkgRoot,
  [string]$RepoRoot,
  [string]$BuildRoot,
  [string]$Triplet
) {
  $vcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
  if (-not (Test-Path $vcpkgExe)) { throw "vcpkg.exe not found at: $vcpkgExe" }

  $logPath = Join-Path $BuildRoot "vcpkg-manifest-install.log"
  $installedDir = Join-Path $BuildRoot "vcpkg_installed"

  Ensure-Dir $BuildRoot
  Ensure-Dir $installedDir

  # Force consistent triplet (common cause of manifest failures when manifest excludes staticcrt)
  $env:VCPKG_DEFAULT_TRIPLET = $Triplet

  Write-Host ""
  Write-Host "== vcpkg install (manifest mode) =="
  Write-Host "  RepoRoot     : $RepoRoot"
  Write-Host "  Triplet      : $Triplet"
  Write-Host "  InstallRoot  : $installedDir"
  Write-Host "  Log          : $logPath"
  Write-Host ""

  # Always create/overwrite log
  if (Test-Path $logPath) { Remove-Item -Force $logPath }

  # Run manifest install explicitly and tee output to log.
  # --x-manifest-root uses the repo's vcpkg.json manifest. :contentReference[oaicite:4]{index=4}
  & $vcpkgExe install `
    --triplet $Triplet `
    --x-manifest-root $RepoRoot `
    --x-install-root $installedDir `
    --clean-after-build 2>&1 | Tee-Object -FilePath $logPath

  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "vcpkg install FAILED. Showing last 200 lines of the log:"
    Write-Host "------------------------------------------------------"
    Get-Content -Path $logPath -Tail 200
    Write-Host "------------------------------------------------------"
    throw "vcpkg install failed (exit $LASTEXITCODE). Full log: $logPath"
  }

  return @{ Log = $logPath; InstalledDir = $installedDir }
}

function Run-CMakeConfigureBuild(
  [string]$RepoRoot,
  [string]$BuildRoot,
  [string]$VcpkgRoot,
  [string]$InstalledDir,
  [string]$Triplet,
  [string]$Generator,
  [string]$Arch,
  [string]$Config,
  [bool]$DoBuild
) {
  $toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
  if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found at: $toolchain" }

  Write-Host ""
  Write-Host "== CMake configure =="
  Write-Host "  Generator     : $Generator"
  Write-Host "  Arch          : $Arch"
  Write-Host "  Config        : $Config"
  Write-Host "  BuildDir      : $BuildRoot"
  Write-Host ""

  # Configure
  & cmake -S $RepoRoot -B $BuildRoot -G $Generator -A $Arch `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DVCPKG_TARGET_TRIPLET="$Triplet" `
    -DVCPKG_HOST_TRIPLET="$Triplet" `
    -DVCPKG_INSTALLED_DIR="$InstalledDir" `
    -DCOLONY_BUILD_TESTS=ON

  if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed (exit $LASTEXITCODE)."
  }

  if (-not $DoBuild) { return }

  Write-Host ""
  Write-Host "== CMake build =="
  & cmake --build $BuildRoot --config $Config --parallel
  if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed (exit $LASTEXITCODE)."
  }
}

# ------------------------- main -------------------------
if (-not $IsWindows) { throw "This script is Windows-only." }

$repo = Resolve-RepoRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
  $BuildDir = Join-Path $repo "build"
} else {
  # Allow relative build dir
  if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo $BuildDir
  }
}

if ($Clean -and (Test-Path $BuildDir)) {
  Write-Host "Cleaning build dir: $BuildDir"
  Remove-Item -Recurse -Force $BuildDir
}

Default-Caching-For-CI

$vcpkgRoot = Find-VcpkgRoot $repo
$installResult = Run-VcpkgInstall -VcpkgRoot $vcpkgRoot -RepoRoot $repo -BuildRoot $BuildDir -Triplet $Triplet

Run-CMakeConfigureBuild `
  -RepoRoot $repo `
  -BuildRoot $BuildDir `
  -VcpkgRoot $vcpkgRoot `
  -InstalledDir $installResult.InstalledDir `
  -Triplet $Triplet `
  -Generator $Generator `
  -Arch $Architecture `
  -Config $Config `
  -DoBuild (-not $NoBuild)

Write-Host ""
Write-Host "DONE."
Write-Host "vcpkg log: $($installResult.Log)"
