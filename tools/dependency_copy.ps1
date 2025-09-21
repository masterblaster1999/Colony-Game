<# 
 Copies common graphics dependencies next to a built .exe.
 - D3DCompiler_47.dll (FXC runtime compiler)
 - (optional) dxcompiler.dll + dxil.dll (DXC + DXIL validator)

 Usage:
   pwsh -NoProfile -ExecutionPolicy Bypass `
        -File tools/dependency_copy.ps1 `
        -ExePath build\bin\ColonyGame.exe `
        -Arch x64 `
        -IncludeDXC

 Notes:
 - Microsoft recommends copying D3DCompiler_47.dll local if you compile shaders at runtime,
   from Windows SDK ...\Redist\D3D\<arch>.  https://learn.microsoft.com/windows/win32/directx-sdk--august-2009-  (#3)
 - DXC bits (dxcompiler.dll, dxil.dll) are shipped via the DirectXShaderCompiler releases/NuGet.
#>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ExePath,

  [ValidateSet('x64', 'x86')]
  [string]$Arch = $(if ([Environment]::Is64BitOperatingSystem) { 'x64' } else { 'x86' }),

  [string]$OutDir = $(Split-Path -Parent $ExePath),

  [switch]$IncludeDXC,

  [switch]$DryRun
)

function Write-Note($msg) { Write-Host "[deps] $msg" }

function Resolve-D3DCompiler47 {
  param([string]$Arch)

  $candidates = @(
    # Preferred redistributable locations (Windows 8.0/8.1/10+ SDKs)
    "$Env:ProgramFiles(x86)\Windows Kits\10\Redist\D3D\$Arch\D3DCompiler_47.dll",
    "$Env:ProgramFiles(x86)\Windows Kits\8.1\Redist\D3D\$Arch\D3DCompiler_47.dll",
    "$Env:ProgramFiles(x86)\Windows Kits\8.0\Redist\D3D\$Arch\D3DCompiler_47.dll"
  )

  # System fallback (good for many Win10/11 installs)
  $sysDir = if ($Arch -eq 'x86' -and [Environment]::Is64BitOperatingSystem) { "$Env:WINDIR\SysWOW64" } else { "$Env:WINDIR\System32" }
  $candidates += @("$sysDir\D3DCompiler_47.dll")

  foreach ($p in $candidates) { if (Test-Path $p) { return (Resolve-Path $p).Path } }
  return $null
}

function Resolve-DXC {
  param([string]$Arch)

  $found = @{}

  # 1) Windows SDK Redist (some SDKs include dxil.dll, sometimes dxcompiler.dll)
  $sdkRedist = "$Env:ProgramFiles(x86)\Windows Kits\10\Redist\D3D\$Arch"
  if (Test-Path $sdkRedist) {
    Get-ChildItem $sdkRedist -Filter dx*.dll | ForEach-Object { $found[$_.Name.ToLower()] = $_.FullName }
  }

  # 2) Windows SDK bin (versioned)
  $sdkBin = "$Env:ProgramFiles(x86)\Windows Kits\10\bin"
  if (Test-Path $sdkBin) {
    Get-ChildItem $sdkBin -Directory | ForEach-Object {
      $binArch = Join-Path $_.FullName $Arch
      if (Test-Path $binArch) {
        Get-ChildItem $binArch -Filter dx*.dll | ForEach-Object { $found[$_.Name.ToLower()] = $_.FullName }
      }
    }
  }

  # 3) NuGet cache (Microsoft.Direct3D.DXC)
  $nugetRoot = Join-Path $Env:USERPROFILE ".nuget\packages\microsoft.direct3d.dxc"
  if (Test-Path $nugetRoot) {
    Get-ChildItem $nugetRoot -Directory | ForEach-Object {
      $paths = @(
        Join-Path $_.FullName "runtimes\win-$Arch\native",
        Join-Path $_.FullName "bin\$Arch"
      )
      foreach ($p in $paths) {
        if (Test-Path $p) {
          Get-ChildItem $p -Filter dx*.dll | ForEach-Object { $found[$_.Name.ToLower()] = $_.FullName }
        }
      }
    }
  }

  # 4) vcpkg (optional)
  if ($Env:VCPKG_ROOT) {
    Get-ChildItem "$Env:VCPKG_ROOT\installed" -Directory -ErrorAction SilentlyContinue | ForEach-Object {
      $bin = Join-Path $_.FullName "bin"
      if (Test-Path $bin) {
        Get-ChildItem $bin -Filter dx*.dll | ForEach-Object { $found[$_.Name.ToLower()] = $_.FullName }
      }
    }
  }

  # Return only the two we care about if present
  $result = @{}
  foreach ($n in @('dxcompiler.dll','dxil.dll')) {
    if ($found.ContainsKey($n)) { $result[$n] = $found[$n] }
  }
  return $result
}

function Copy-IfNewer {
  param([string]$src, [string]$dstDir)

  if (-not (Test-Path $src)) { return $false }
  $dst = Join-Path $dstDir (Split-Path $src -Leaf)
  if ($DryRun) {
    Write-Note "DRY RUN: would copy `"$src`" -> `"$dst`""
    return $true
  }
  New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
  if (-not (Test-Path $dst) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $dst).LastWriteTime)) {
    Copy-Item $src $dst -Force
    Write-Note "copied  $([System.IO.Path]::GetFileName($src))"
  } else {
    Write-Note "up-to-date: $([System.IO.Path]::GetFileName($src))"
  }
  return $true
}

# --- main ---
$exeFull = Resolve-Path $ExePath -ErrorAction Stop
$exeDir  = Split-Path -Parent $exeFull
if (-not $OutDir) { $OutDir = $exeDir }
Write-Note "exe     : $exeFull"
Write-Note "out dir : $OutDir"
Write-Note "arch    : $Arch"

# D3DCompiler_47.dll
$d3d = Resolve-D3DCompiler47 -Arch $Arch
if ($d3d) { Copy-IfNewer -src $d3d -dstDir $OutDir | Out-Null }
else {
  Write-Warning "D3DCompiler_47.dll not found. If you compile HLSL at runtime, copy it from a Windows SDK 'Redist\\D3D\\$Arch' folder. See Microsoft guidance."
}

# DXC (optional)
if ($IncludeDXC) {
  $dxc = Resolve-DXC -Arch $Arch
  if ($dxc.Count -gt 0) {
    foreach ($path in $dxc.Values) { Copy-IfNewer -src $path -dstDir $OutDir | Out-Null }
  } else {
    Write-Warning "DXC (dxcompiler.dll/dxil.dll) not found. Install the NuGet package 'Microsoft.Direct3D.DXC' or grab them from the DXC GitHub releases."
  }
}

Write-Note "done."
