#Requires -Version 5.1
<#
.SYNOPSIS
  Bootstraps a Windows C++ dev environment for this repo:
  - Ensures MSVC dev shell (Visual Studio Build Tools) is active
  - Verifies or installs CMake, Ninja, Git (optional via winget)
  - Finds or installs vcpkg; sets VCPKG_ROOT
  - (Optional) Updates vcpkg manifest baseline and installs dependencies
  - (Optional) Configures the CMake build with the vcpkg toolchain

.PARAMETER Arch
  Target architecture for MSVC environment. Use 'amd64' for 64-bit builds.

.PARAMETER Generator
  CMake generator to use for the first configure. 'Ninja' or 'Visual Studio 17 2022'.

.PARAMETER BuildDir
  Build directory for CMake configure.

.PARAMETER VcpkgRoot
  Path to a vcpkg instance. If omitted, tries $env:VCPKG_ROOT, then common locations.
  If not found and -InstallVcpkg is set, the script will clone + bootstrap vcpkg
  into '<repo>/extern/vcpkg'.

.PARAMETER InstallMissingTools
  If set, attempts to install missing CMake/Ninja/Git via winget.

.PARAMETER InstallVSBuildTools
  If set, attempts to install Visual Studio 2022 Build Tools (VC workload) via winget.

.PARAMETER UpdateBaseline
  If set and a vcpkg.json is found at repo root, runs:
    vcpkg x-update-baseline [--add-initial-baseline]

.PARAMETER ManifestInstall
  If set, runs 'vcpkg install' in manifest mode for this repo.

.PARAMETER Configure
  If set, runs an initial 'cmake -S . -B <BuildDir>' with the vcpkg toolchain.

.EXAMPLE
  # Fast path: verify tools, enter MSVC shell, ensure vcpkg, install deps, configure Ninja build
  pwsh -File tools/setup-dev.ps1 -ManifestInstall -Configure

.EXAMPLE
  # Do installs with winget if missing; also install VS Build Tools if not present
  pwsh -File tools/setup-dev.ps1 -InstallMissingTools -InstallVSBuildTools -ManifestInstall -Configure

.NOTES
  - Detects an existing Visual Studio developer shell via $env:VSCMD_VER.
  - Uses Launch-VsDevShell.ps1 when available; falls back to VsDevCmd.bat -arch/-host_arch.
  - In CMake configure, uses vcpkg.cmake toolchain if VCPKG_ROOT can be resolved.
#>

[CmdletBinding()]
param(
  [ValidateSet('x86','amd64','arm','arm64')]
  [string]$Arch = 'amd64',

  [ValidateSet('Ninja','Visual Studio 17 2022')]
  [string]$Generator = 'Ninja',

  [string]$BuildDir = 'build',

  [string]$VcpkgRoot,

  [switch]$InstallMissingTools,
  [switch]$InstallVSBuildTools,
  [switch]$InstallVcpkg,

  [switch]$UpdateBaseline,
  [switch]$ManifestInstall,
  [switch]$Configure
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Section($msg) { Write-Host "`n== $msg ==" -ForegroundColor Cyan }
function Write-Note($msg)    { Write-Host "[i] $msg" -ForegroundColor DarkCyan }
function Write-Warn2($msg)   { Write-Warning $msg }
function Die($msg) { throw $msg }

# Resolve repo root (this script lives in repo/tools/)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir '..')

# --- Helpers ---------------------------------------------------------------

function Test-Command($name) {
  return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Ensure-WinGet() {
  if (-not (Test-Command 'winget.exe')) {
    Die "winget not found. Install 'App Installer' from Microsoft Store or see https://learn.microsoft.com/windows/package-manager/winget/"
  }
}

function Winget-Install($id, [string]$extraArgs = '') {
  Ensure-WinGet
  Write-Note "Installing via winget: $id $extraArgs"
  $cmd = "winget install -e --id $id"
  if ($extraArgs) { $cmd += " --override `"$extraArgs`"" }
  & powershell -NoProfile -ExecutionPolicy Bypass -Command $cmd
}

function Find-VsInstallPath {
  # Prefer vswhere (handles Build Tools via -products *)
  $vswhereDefault = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $vswhere = $null
  if (Test-Path $vswhereDefault) { $vswhere = $vswhereDefault }
  elseif (Test-Command 'vswhere.exe') { $vswhere = (Get-Command vswhere.exe).Source }

  if (-not $vswhere) {
    Write-Warn2 "vswhere.exe not found. If VS Build Tools are installed, we will attempt known paths."
    $candidates = @(
      "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools",
      "$env:ProgramFiles\Microsoft Visual Studio\2022\Community",
      "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional",
      "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
  }

  # Require VC tools to be present
  $path = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
  if (-not $path) {
    # as a fallback, any VS instance
    $path = & $vswhere -products * -latest -property installationPath
  }
  return ($path | Select-Object -First 1)
}

function Enter-MsvcDevShell {
  if ($env:VSCMD_VER) {
    Write-Note "MSVC developer environment already active (VSCMD_VER=$($env:VSCMD_VER))."
    return
  }

  $vsPath = Find-VsInstallPath
  if (-not $vsPath) {
    if ($InstallVSBuildTools) {
      Write-Section "Installing Visual Studio 2022 Build Tools (VC workload) via winget..."
      # Install Build Tools + VC workload; use documented IDs/override
      Winget-Install 'Microsoft.VisualStudio.2022.BuildTools' '--passive --wait --add Microsoft.VisualStudio.Workload.VCTools'
      $vsPath = Find-VsInstallPath
    } else {
      Die "Visual Studio Build Tools not found. Re-run with -InstallVSBuildTools or install manually."
    }
  }

  Write-Note "Using Visual Studio at: $vsPath"

  # Prefer Developer PowerShell (Launch-VsDevShell.ps1), otherwise VsDevCmd.bat
  $launch = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
  $devcmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'

  if (Test-Path $launch) {
    Write-Note "Launching Developer PowerShell (Launch-VsDevShell.ps1) -Arch $Arch -HostArch $Arch ..."
    # Keep current directory; -SkipAutomaticLocation avoids jumping folders
    & $launch -Arch $Arch -HostArch $Arch -SkipAutomaticLocation | Out-Null
  elseif (Test-Path $devcmd) {
    Write-Note "Running VsDevCmd.bat (-arch=$Arch -host_arch=$Arch) and importing environment..."
    # Import environment from VsDevCmd into current PowerShell
    $envBlock = cmd.exe /c "`"$devcmd`" -arch=$Arch -host_arch=$Arch & set"
    $envBlock -split "`r?`n" | ForEach-Object {
      $kv = $_ -split '=', 2
      if ($kv.Length -eq 2) { [System.Environment]::SetEnvironmentVariable($kv[0], $kv[1]) }
    }
  } else {
    Die "Could not find Launch-VsDevShell.ps1 or VsDevCmd.bat under $vsPath\Common7\Tools"
  }

  if (-not $env:VSCMD_VER) {
    Write-Warn2 "Developer environment might not have initialized correctly (VSCMD_VER is unset)."
  }
}

function Ensure-Tooling {
  Write-Section "Checking base toolchain (CMake, Ninja, Git)..."
  if (-not (Test-Command 'cmake.exe')) {
    if ($InstallMissingTools) { Winget-Install 'Kitware.CMake' }
    else { Write-Warn2 "CMake not found. Re-run with -InstallMissingTools or install Kitware.CMake." }
  }
  if ($Generator -eq 'Ninja' -and -not (Test-Command 'ninja.exe')) {
    if ($InstallMissingTools) { Winget-Install 'Ninja-build.Ninja' }
    else { Write-Warn2 "Ninja not found. Re-run with -InstallMissingTools or install Ninja-build.Ninja." }
  }
  if (-not (Test-Command 'git.exe')) {
    if ($InstallMissingTools) { Winget-Install 'Git.Git' }
    else { Write-Warn2 "Git not found. Re-run with -InstallMissingTools or install Git.Git." }
  }
}

function Find-Vcpkg {
  if ($VcpkgRoot) {
    if (Test-Path $VcpkgRoot) { return (Resolve-Path $VcpkgRoot) }
    Die "VcpkgRoot '$VcpkgRoot' does not exist."
  }

  if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
    return (Resolve-Path $env:VCPKG_ROOT)
  }

  $candidates = @(
    Join-Path $RepoRoot 'extern\vcpkg',
    'C:\vcpkg',
    Join-Path $RepoRoot 'vcpkg'
  )
  foreach ($c in $candidates) { if (Test-Path $c) { return (Resolve-Path $c) } }

  return $null
}

function Install-Vcpkg($dest) {
  Write-Section "Installing vcpkg to: $dest"
  if (-not (Test-Path (Split-Path $dest))) { New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null }
  if (-not (Test-Command 'git.exe')) { Die "git is required to clone vcpkg. Install Git or use -InstallMissingTools." }
  git clone https://github.com/microsoft/vcpkg.git --depth=1 $dest | Write-Verbose
  $bootstrap = Join-Path $dest 'bootstrap-vcpkg.bat'
  if (-not (Test-Path $bootstrap)) { Die "bootstrap-vcpkg.bat not found after clone: $bootstrap" }
  & $bootstrap -disableMetrics
}

function Ensure-Vcpkg {
  Write-Section "Resolving vcpkg..."
  $root = Find-Vcpkg
  if (-not $root) {
    if ($InstallVcpkg) {
      $root = Join-Path $RepoRoot 'extern\vcpkg'
      Install-Vcpkg -dest $root
    } else {
      Die "vcpkg not found. Re-run with -InstallVcpkg to clone+bootstrap, or set -VcpkgRoot / $env:VCPKG_ROOT."
    }
  }
  $global:VCPKG_ROOT = $root.Path
  $env:VCPKG_ROOT = $global:VCPKG_ROOT  # Per docs, tools can use VCPKG_ROOT if provided
  Write-Note "VCPKG_ROOT = $env:VCPKG_ROOT"

  $exe = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
  if (-not (Test-Path $exe)) {
    $bootstrap = Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat'
    if (Test-Path $bootstrap) {
      & $bootstrap -disableMetrics
    } else {
      Die "vcpkg.exe not found and bootstrap script missing at $env:VCPKG_ROOT"
    }
  }

  return $exe
}

function Update-VcpkgBaseline($vcpkgExe) {
  $manifest = Join-Path $RepoRoot 'vcpkg.json'
  if (-not (Test-Path $manifest)) {
    Write-Note "No vcpkg.json at repo root; skipping baseline update."
    return
  }
  $json = Get-Content $manifest -Raw | ConvertFrom-Json
  $hasBaseline = $null -ne $json.'builtin-baseline'
  $args = @('x-update-baseline')
  if (-not $hasBaseline) { $args += '--add-initial-baseline' }
  Write-Section "vcpkg baseline update ($(if ($hasBaseline) {'refresh'} else {'add initial'}))..."
  & $vcpkgExe @args --x-manifest-root="$RepoRoot"
}

function Run-VcpkgManifestInstall($vcpkgExe) {
  Write-Section "vcpkg manifest install for this repo..."
  & $vcpkgExe install --triplet "$(
      if ($Arch -eq 'amd64') {'x64-windows'}
      elseif ($Arch -eq 'x86') {'x86-windows'}
      elseif ($Arch -eq 'arm64') {'arm64-windows'}
      else {'x86-windows'}
    )" --x-manifest-root="$RepoRoot"
}

function Run-CMakeConfigure {
  Write-Section "Configuring CMake build ($Generator) into '$BuildDir'..."
  $toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
  $triplet = if ($Arch -eq 'amd64') {'x64-windows'}
             elseif ($Arch -eq 'x86') {'x86-windows'}
             elseif ($Arch -eq 'arm64') {'arm64-windows'}
             else {'x86-windows'}

  $args = @('-S', $RepoRoot, '-B', (Join-Path $RepoRoot $BuildDir))
  $args += @('-D', "CMAKE_TOOLCHAIN_FILE=$toolchain")
  $args += @('-D', "VCPKG_TARGET_TRIPLET=$triplet")

  if ($Generator -eq 'Ninja') {
    $args = @('-G','Ninja') + $args
  } else {
    # Visual Studio generator: also pass architecture
    $args = @('-G','Visual Studio 17 2022','-A','x64') + $args
  }

  cmake @args
}

# --- Main -----------------------------------------------------------------

Write-Section "Setup Dev Environment"
Write-Note "Repo root: $RepoRoot"

# 1) Enter MSVC dev shell (unless already there)
Enter-MsvcDevShell

# 2) Check core tools (optionally install via winget)
Ensure-Tooling

# 3) vcpkg (resolve or install)
$vcpkgExe = Ensure-Vcpkg

# 4) Optional: update baseline
if ($UpdateBaseline) {
  Update-VcpkgBaseline -vcpkgExe $vcpkgExe
}

# 5) Optional: manifest install
if ($ManifestInstall) {
  Run-VcpkgManifestInstall -vcpkgExe $vcpkgExe
}

# 6) Optional: CMake configure
if ($Configure) {
  Run-CMakeConfigure
}

Write-Host "`nAll done." -ForegroundColor Green
