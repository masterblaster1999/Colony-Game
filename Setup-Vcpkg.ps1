<# =================================================================================================
  Setup-Vcpkg.ps1
  --------------------------------------------------------------------------------------------------
  Purpose
    Fix “toolchain preset vs vcpkg location” by adding vcpkg as a Git submodule at ./vcpkg,
    bootstrapping it, and (optionally) ensuring your CMakePresets point to the local toolchain file.

  What this single script does
    1) Verifies you’re at a Git repo root (has .git) and CMakeLists.txt exists.
    2) Adds vcpkg as a submodule at ./vcpkg (idempotent: updates/initializes if already present).
    3) Bootstraps vcpkg (Windows: .\vcpkg\bootstrap-vcpkg.bat; Others: ./vcpkg/bootstrap-vcpkg.sh).
    4) Sets VCPKG_ROOT for this session.
    5) If CMakePresets.json exists, backs it up and ensures CMAKE_TOOLCHAIN_FILE points to:
         ${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake
       (Only touches presets that don’t already point at a vcpkg toolchain.)
    6) Prints concrete, repo-specific build, install, and (optional) package commands including paths.

  Prerequisites
    - Git, CMake (>=3.21 recommended), and either VS2022 (MSVC) or Ninja + a compiler.
    - PowerShell 7+ recommended (Windows has it by default as "pwsh" if installed, or "powershell").

  How to run (from your repo root)
    pwsh -ExecutionPolicy Bypass -File .\Setup-Vcpkg.ps1
    # or:
    powershell -ExecutionPolicy Bypass -File .\Setup-Vcpkg.ps1

  After it runs, look for:
    - vcpkg/               (submodule)
    - CMakePresets.json.bak (backup created if we edited your presets)
    - Console output with build + install commands and paths tailored to your presets.

  Notes
    - This script *does not* commit anything. Review changes (`git status`) and commit when happy.
    - If your presets already point to ${sourceDir}/vcpkg/... we won’t overwrite them.
================================================================================================= #>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Info   { param($m) Write-Host "[INFO]  $m" -ForegroundColor Cyan }
function Write-Warn   { param($m) Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-ErrorX { param($m) Write-Error  "[ERROR] $m" }

function Assert-AtRepoRoot {
  if (-not (Test-Path ".git")) { Write-ErrorX "This does not look like a Git repo root (missing .git)."; exit 1 }
  if (-not (Test-Path "CMakeLists.txt")) { Write-ErrorX "Missing CMakeLists.txt in current directory."; exit 1 }
}

function Ensure-Vcpkg-Submodule {
  $vcpkgDir = Join-Path (Get-Location) "vcpkg"
  if (Test-Path (Join-Path $vcpkgDir ".git")) {
    Write-Info "vcpkg submodule already present at ./vcpkg — initializing/updating…"
    & git submodule update --init --recursive vcpkg | Out-Null
  } else {
    if (Test-Path $vcpkgDir) {
      Write-Warn "A folder named 'vcpkg' exists but is not a submodule. We will convert it into a submodule."
      Write-Info "Renaming existing 'vcpkg' to 'vcpkg._backup' just in case…"
      Rename-Item -Path $vcpkgDir -NewName "vcpkg._backup" -ErrorAction Stop
    }
    Write-Info "Adding vcpkg as a submodule at ./vcpkg…"
    & git submodule add https://github.com/microsoft/vcpkg.git vcpkg | Out-Null
    & git submodule update --init --recursive vcpkg | Out-Null
  }
  Write-Info "vcpkg submodule ready."
}

function Bootstrap-Vcpkg {
  $vcpkgDir = Join-Path (Get-Location) "vcpkg"
  if ($IsWindows) {
    $bootstrapBat = Join-Path $vcpkgDir "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapBat)) { Write-ErrorX "Missing $bootstrapBat"; exit 1 }
    Write-Info "Bootstrapping vcpkg (Windows)…"
    & $bootstrapBat -disableMetrics | Out-Null
  } else {
    $bootstrapSh = Join-Path $vcpkgDir "bootstrap-vcpkg.sh"
    if (-not (Test-Path $bootstrapSh)) { Write-ErrorX "Missing $bootstrapSh"; exit 1 }
    Write-Info "Bootstrapping vcpkg (Unix)…"
    & bash -lc "'$bootstrapSh' -disableMetrics" | Out-Null
  }
  $env:VCPKG_ROOT = $vcpkgDir
  Write-Info "VCPKG_ROOT set to $($env:VCPKG_ROOT)"
}

function Update-CMakePresets-Toolchain {
  $presetPath = Join-Path (Get-Location) "CMakePresets.json"
  if (-not (Test-Path $presetPath)) {
    Write-Warn "CMakePresets.json not found; skipping preset patch (not required if your toolchain is already correct)."
    return $null
  }

  Write-Info "Inspecting CMakePresets.json…"
  $jsonText = Get-Content -Raw -Path $presetPath
  try {
    $json = $jsonText | ConvertFrom-Json -Depth 64
  } catch {
    Write-Warn "CMakePresets.json is not plain JSON (or contains comments). Skipping automated edit."
    return $null
  }

  if (-not $json.configurePresets) {
    Write-Warn "No 'configurePresets' found. Skipping automated edit."
    return $null
  }

  $toolchainValue = '${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake'
  $patched = $false
  foreach ($p in $json.configurePresets) {
    if (-not $p.cacheVariables) { $p | Add-Member -NotePropertyName cacheVariables -NotePropertyValue (@{}) }
    $existing = $p.cacheVariables.CMAKE_TOOLCHAIN_FILE
    if ($existing) {
      # If it already points to a vcpkg toolchain, keep it as-is
      if ($existing -match 'vcpkg[/\\]scripts[/\\]buildsystems[/\\]vcpkg\.cmake') {
        continue
      }
    }
    $p.cacheVariables.CMAKE_TOOLCHAIN_FILE = $toolchainValue
    $patched = $true
  }

  if ($patched) {
    $backup = "$presetPath.bak"
    Copy-Item $presetPath $backup -Force
    ($json | ConvertTo-Json -Depth 64) | Out-File -Encoding UTF8 -FilePath $presetPath
    Write-Info "Updated CMakePresets.json (backup at $(Split-Path -Leaf $backup))."
  } else {
    Write-Info "CMakePresets already reference a vcpkg toolchain; no change made."
  }

  return $json
}

function Pick-ConfigurePreset {
  param($json)
  if (-not $json) { return $null }
  if (-not $json.configurePresets) { return $null }
  # Prefer a Windows/Ninja-ish preset name if available; else first
  $candidates = $json.configurePresets
  $preferred  = $candidates | Where-Object { $_.name -match '(vs|win|msvc|ninja|x64)' } | Select-Object -First 1
  if ($preferred) { return $preferred }
  return ($candidates | Select-Object -First 1)
}

function Find-BuildPresetFor {
  param($json, $configureName)
  if (-not $json -or -not $json.buildPresets) { return $null }
  $match = $json.buildPresets | Where-Object { $_.configurePreset -eq $configureName } | Select-Object -First 1
  return $match
}

function Compute-BinaryDir {
  param($configurePreset)
  if (-not $configurePreset) { return $null }
  if ($configurePreset.binaryDir) {
    return $configurePreset.binaryDir
  } else {
    # CMake default if binaryDir is omitted:
    return "out/build/$($configurePreset.name)"
  }
}

# ----------------- Main flow -----------------
Assert-AtRepoRoot
Ensure-Vcpkg-Submodule
Bootstrap-Vcpkg
$json = Update-CMakePresets-Toolchain

# Show tailored build/install guidance
Write-Host ""
Write-Host "======================= NEXT STEPS (tailored to your repo) =======================" -ForegroundColor Green

$cfgPreset   = Pick-ConfigurePreset -json $json
$cfgName     = if ($cfgPreset) { $cfgPreset.name } else { "<your-configure-preset>" }
$binDir      = Compute-BinaryDir -configurePreset $cfgPreset
$buildPreset = Find-BuildPresetFor -json $json -configureName $cfgName

if ($cfgPreset) {
  Write-Info "Detected configure preset: $cfgName"
  Write-Info "Binary dir (as defined/assumed by preset): $binDir"
} else {
  Write-Warn "Could not detect a configure preset. Below are generic commands."
}

# Choose an install target directory under the repo
$repoRoot = (Get-Location).Path
$installDebug   = Join-Path $repoRoot "_install/debug"
$installRelease = Join-Path $repoRoot "_install/release"

Write-Host ""
Write-Host "Configure (uses local vcpkg toolchain at ./vcpkg):" -ForegroundColor Cyan
if ($cfgPreset) {
  Write-Host "  cmake --preset $cfgName"
} else {
  Write-Host "  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=`"${repoRoot}\vcpkg\scripts\buildsystems\vcpkg.cmake`""
}

Write-Host ""
Write-Host "Build:" -ForegroundColor Cyan
if ($buildPreset) {
  Write-Host "  cmake --build --preset $($buildPreset.name)"
} elseif ($cfgPreset) {
  Write-Host "  cmake --build `"$binDir`""
} else {
  Write-Host "  cmake --build build"
}

Write-Host ""
Write-Host "Install (single-config generators, e.g., Ninja):" -ForegroundColor Cyan
if ($cfgPreset) {
  Write-Host "  cmake --install `"$binDir`" --prefix `"$installDebug`""
} else {
  Write-Host "  cmake --install build --prefix `"$installDebug`""
}

Write-Host ""
Write-Host "Install (multi-config generators, e.g., Visual Studio with Debug/Release):" -ForegroundColor Cyan
if ($cfgPreset) {
  Write-Host "  cmake --install `"$binDir`" --config Release --prefix `"$installRelease`""
  Write-Host "  cmake --install `"$binDir`" --config Debug   --prefix `"$installDebug`""
} else {
  Write-Host "  cmake --install build --config Release --prefix `"$installRelease`""
  Write-Host "  cmake --install build --config Debug   --prefix `"$installDebug`""
}

Write-Host ""
Write-Host "Optional: create a ZIP via CPack (if your project configures it):" -ForegroundColor Cyan
if ($cfgPreset) {
  Write-Host "  cpack -C Release -B `"$repoRoot\_artifacts`" -G ZIP"
} else {
  Write-Host "  cpack -C Release -B `"$repoRoot\_artifacts`" -G ZIP"
}

Write-Host ""
Write-Host "All set. Review changes (e.g., '.gitmodules' and 'CMakePresets.json.bak'), then commit:" -ForegroundColor Green
Write-Host "  git add .gitmodules vcpkg"
if (Test-Path "CMakePresets.json.bak") { Write-Host "  git add CMakePresets.json" }
Write-Host "  git commit -m `"Add vcpkg submodule at ./vcpkg and set up local toolchain`""
Write-Host "=================================================================================="
