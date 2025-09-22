<# 
  Colony-Game — HLSL offline compiler (Windows-only) — Ultra Edition

  Highlights vs. prior version:
    • Auto-discovery of FXC/DXC via Windows SDK + vswhere (and PATH). Warn on Vulkan SDK DXC in PATH.
    • FXC default (D3D11 / SM5.x → DXBC), DXC opt-in (SM6+/DXIL).
    • Parallel compile pipeline with bounded workers; CI-friendly logs and exit codes.
    • Watch mode: rebuilds only impacted shaders when .hlsl/.hlsli/.hlsl.json change (include graph).
    • Sidecar JSON per file: multiple stages, entries, profile overrides, per-job defines & permutations.
    • Global permutations (-Permutations NAME=V;A=B), merged with sidecar permutations.
    • Deterministic content hashing (of source, includes, defines, profile, entry, compiler, config).
    • Outputs: .cso (+ .pdb in Debug), optional disassembly (.asm), optional C header (.h) with byte arrays.
    • Per-job depfiles (.d) and a consolidated manifest.json.
    • Optional packaging: ZIP (default) or a simple .pak with index; deterministic order.
    • Clean/Force/List-only and rich CLI with colors (toggle -NoColor / -Quiet).

  Notes:
    - FXC CLI switches (/T, /E, /Fo, /Fd, /Zi, /O3, /Qstrip_debug, /Qstrip_reflect, /WX, /Vd) per Microsoft Learn. 
    - DXC CLI (-T, -E, -Fo, -Fd, -Zi, -Qembed_debug, -Qstrip_debug, -Qstrip_reflect, -Vd) per DXC wiki/docs.
    - vswhere is the recommended way to locate VS toolchains when needed.
    - Beware Vulkan SDK dxc.exe precedence in PATH; we detect and warn. 
    (See sources in the PR/commit message.)
#>

[CmdletBinding()]
param(
  # Input & output roots
  [string]$ShaderDir = (Join-Path (Split-Path -Parent $PSCommandPath) "..\..\src\render\d3d11\shaders"),
  [string]$OutDir    = (Join-Path (Split-Path -Parent $PSCommandPath) "..\..\res\shaders"),

  # Build configuration
  [ValidateSet("Debug","Release")]
  [string]$Config = "Debug",
  [switch]$WarningsAsErrors,
  [switch]$Force,
  [switch]$DeterministicHash,
  [switch]$ListOnly,
  [switch]$Quiet,
  [switch]$NoColor,

  # Compiler selection
  [switch]$UseDXC,              # default: FXC for D3D11
  [hashtable]$Profiles = @{},   # override stage->profile (e.g. @{ cs='cs_5_1' })
  [string[]]$IncludeDirs = @(),
  [string[]]$Defines     = @(),

  # Advanced outputs
  [switch]$EmitDisasm,          # .asm
  [switch]$EmitHeaders,         # .h (C arrays); FXC only (DXC -Fh not used for DXBC)
  [switch]$EmbedDebug,          # DXC: -Qembed_debug with -Zi
  [switch]$DisableValidation,   # /Vd or -Vd

  # Permutations
  [string[]]$Permutations = @(),  # each element: "NAME=V;FLAG;OTHER=1" (semicolon or comma separated)

  # Parallelism & watch
  [int]$MaxJobs = [Math]::Max([Environment]::ProcessorCount - 1, 1),
  [switch]$Watch,

  # Packaging
  [switch]$Pack,
  [ValidateSet("zip","pak")]
  [string]$PackType = "zip",
  [string]$PackName = "shaders",

  # Tool overrides
  [string]$FXCPath = "",
  [string]$DXCPath = "",

  # Manifest
  [string]$ManifestPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$global:CG_NoColor = $NoColor

#--------------------------------- Logging ------------------------------------
function Write-Info([string]$m){ if($Quiet){return}; if($global:CG_NoColor){Write-Host $m;return}; Write-Host $m -ForegroundColor Cyan }
function Write-Ok([string]$m)  { if($Quiet){return}; if($global:CG_NoColor){Write-Host $m;return}; Write-Host $m -ForegroundColor Green }
function Write-Warn2([string]$m){ if($global:CG_NoColor){Write-Warning $m;return}; Write-Host "WARN: $m" -ForegroundColor Yellow }
function Write-Err2([string]$m) { if($global:CG_NoColor){Write-Error $m;return}; Write-Host "ERROR: $m" -ForegroundColor Red }

#--------------------------------- Paths --------------------------------------
function Resolve-Dir([string]$p){
  if(-not $p){ return $null }
  $rp = (Resolve-Path -LiteralPath $p -ErrorAction SilentlyContinue)
  if($rp){ return $rp.ProviderPath }
  New-Item -ItemType Directory -Force -Path $p | Out-Null
  return (Resolve-Path -LiteralPath $p).ProviderPath
}

# Allow environment overrides (useful in CI)
if($env:COLONY_SHADER_DIR){ $ShaderDir = $env:COLONY_SHADER_DIR }
if($env:COLONY_SHADER_OUT){ $OutDir    = $env:COLONY_SHADER_OUT }

$ScriptDir = Split-Path -Parent $PSCommandPath
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..\..")

$ShaderDir = Resolve-Dir $ShaderDir
if(-not (Test-Path $ShaderDir)){
  $fallback = Resolve-Dir (Join-Path $RepoRoot "src\shaders")
  if($fallback -and (Test-Path $fallback)){ Write-Warn2 "ShaderDir not found, using fallback: $fallback"; $ShaderDir = $fallback }
  else { throw "Shader directory not found: $ShaderDir" }
}
$OutDir = Resolve-Dir $OutDir
if(-not $ManifestPath){ $ManifestPath = Join-Path $OutDir "manifest.json" }

# Output subfolders (organized for clarity and packaging)
$OutCSO  = Resolve-Dir (Join-Path $OutDir "bin")
$OutPDB  = Resolve-Dir (Join-Path $OutDir "pdb")
$OutASM  = Resolve-Dir (Join-Path $OutDir "asm")
$OutHDR  = Resolve-Dir (Join-Path $OutDir "headers")
$OutLOG  = Resolve-Dir (Join-Path $OutDir "logs")
$OutDEP  = Resolve-Dir (Join-Path $OutDir "dep")
$CacheDir= Resolve-Dir (Join-Path $OutDir "cache")
$PkgDir  = Resolve-Dir (Join-Path $OutDir "pkg")

# Include roots: shader dir + res\shaders\include (if exists) + user-specified
$defaultInc = @($ShaderDir)
$maybeInc = Join-Path $RepoRoot "res\shaders\include"
if(Test-Path $maybeInc){ $defaultInc += $maybeInc }
$IncludeDirs = @($defaultInc + $IncludeDirs | Select-Object -Unique)

#--------------------------------- Clean --------------------------------------
if($PSBoundParameters.ContainsKey('Clean') -and $Clean){
  Write-Info "Cleaning $OutDir ..."
  foreach($d in @($OutCSO,$OutPDB,$OutASM,$OutHDR,$OutLOG,$OutDEP,$CacheDir,$PkgDir)){
    if(Test-Path $d){ Get-ChildItem -LiteralPath $d -Recurse -File | Remove-Item -Force -ErrorAction SilentlyContinue }
  }
  if(Test-Path $ManifestPath){ Remove-Item -Force -LiteralPath $ManifestPath -ErrorAction SilentlyContinue }
  Write-Ok "Done."
  exit 0
}

#-------------------------- SDK/VS tool discovery -----------------------------
function Get-WindowsSdkBinDirs {
  $dirs=@()
  $root="${env:ProgramFiles(x86)}\Windows Kits\10\bin"
  if(Test-Path $root){
    $vers = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    foreach($v in $vers){ $x64 = Join-Path $v.FullName "x64"; if(Test-Path $x64){ $dirs += $x64 } }
  }
  $old="${env:ProgramFiles(x86)}\Windows Kits\8.1\bin\x64"
  if(Test-Path $old){ $dirs += $old }
  return $dirs
}
function Find-Exe([string]$exe,[string[]]$extra){
  $explicit = if($exe -eq "fxc.exe"){$FXCPath}elseif($exe -eq "dxc.exe"){$DXCPath}else{""}
  if($explicit -and (Test-Path $explicit)){ return (Resolve-Path $explicit).ProviderPath }
  $cmd = Get-Command $exe -ErrorAction SilentlyContinue
  if($cmd){ return $cmd.Source }
  foreach($d in $extra){ $p = Join-Path $d $exe; if(Test-Path $p){ return (Resolve-Path $p).ProviderPath } }
  return $null
}
function Get-VSWhere {
  $p = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if(Test-Path $p){ return $p }
  return $null
}
function Get-DXCFromVS {
  $vsw = Get-VSWhere
  if(-not $vsw){ return $null }
  $json = & $vsw -latest -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -products * -format json | ConvertFrom-Json
  if(-not $json){ return $null }
  foreach($inst in $json){
    $bin = Join-Path $inst.installationPath "VC\Tools\Llvm\x64\bin\dxc.exe"
    if(Test-Path $bin){ return $bin }
  }
  return $null
}

$binDirs = Get-WindowsSdkBinDirs
$FXC     = Find-Exe "fxc.exe" $binDirs
$DXC     = if($UseDXC){ Find-Exe "dxc.exe" $binDirs } else { $null }
if($UseDXC -and -not $DXC){ $DXC = Get-DXCFromVS }

# Warn: Vulkan SDK dxc precedence (can lack DirectX features or behave differently)
if($UseDXC){
  try{
    $where = & where.exe dxc 2>$null
    if($where){
      $lines = $where -split "`r?`n" | Where-Object { $_ -ne "" }
      $vk = $lines | Where-Object { $_ -like "*VulkanSDK*" }
      if($vk){ Write-Warn2 "DXC from Vulkan SDK appears in PATH first: $($vk -join ', '). Prefer Windows SDK/VS DXC for DirectX. (Run 'where dxc' to verify.)" }
    }
  }catch{}
}

if(-not $UseDXC){
  if(-not $FXC){ throw "FXC not found. Install the Windows 10/11 SDK or add fxc.exe to PATH." }
  Write-Info "Compiler: FXC  ($FXC)"
}else{
  if(-not $DXC){ Write-Warn2 "DXC requested but not found; falling back to FXC."; $UseDXC=$false }
  if($UseDXC){ Write-Info "Compiler: DXC  ($DXC)" }
  if(-not $UseDXC -and -not $FXC){ throw "Neither DXC nor FXC found. Install Windows SDK (fxc) and/or VS (dxc)." }
  if(-not $UseDXC){ Write-Info "Compiler: FXC  ($FXC)" }
}

#----------------------------- Stage/profile maps ------------------------------
$DefaultSm5 = @{ cs='cs_5_0'; vs='vs_5_0'; ps='ps_5_0'; gs='gs_5_0'; ds='ds_5_0'; hs='hs_5_0' }
$DefaultSm6 = @{ cs='cs_6_6'; vs='vs_6_6'; ps='ps_6_6'; gs='gs_6_6'; ds='ds_6_6'; hs='hs_6_6' }
$StageSuffixes = @{
  ".cs.hlsl"="cs"; ".vs.hlsl"="vs"; ".ps.hlsl"="ps"; ".gs.hlsl"="gs"; ".ds.hlsl"="ds"; ".hs.hlsl"="hs";
  "_cs.hlsl"="cs"; "_vs.hlsl"="vs"; "_ps.hlsl"="ps"; "_gs.hlsl"="gs"; "_ds.hlsl"="ds"; "_hs.hlsl"="hs"
}
function Stage-Profile([string]$stage){
  if($Profiles.ContainsKey($stage)){ return $Profiles[$stage] }
  if($UseDXC){ return $DefaultSm6[$stage] } else { return $DefaultSm5[$stage] }
}

#----------------------------- Hint parsing & stage sniffing -------------------
function Read-HeaderHints([string]$path){
  $h=@{ stage=$null; entries=@(); profile=$null }
  $reader=[System.IO.File]::OpenText($path)
  try{
    for($i=0;$i -lt 80;$i++){
      $line=$reader.ReadLine(); if($null -eq $line){break}
      if($line -match '^\s*//\s*@stage\s*:\s*([a-z]+)\s*$'){ $h.stage=$Matches[1].ToLowerInvariant() }
      elseif($line -match '^\s*//\s*@entry\s*:\s*(.+?)\s*$'){
        $list = $Matches[1] -split '[,\s;]+' | Where-Object { $_ -ne "" }
        $h.entries = @($list)
      }
      elseif($line -match '^\s*//\s*@profile\s*:\s*([A-Za-z0-9_]+)\s*$'){ $h.profile=$Matches[1] }
    }
  } finally { $reader.Close() }
  return $h
}
function Guess-StageFromName([string]$path){
  $low=$path.ToLowerInvariant()
  foreach($kv in $StageSuffixes.GetEnumerator()){ if($low.EndsWith($kv.Key)){ return $kv.Value } }
  if($low -like "*\compute\*"){ return "cs" }
  if($low -like "*\vertex\*"){ return "vs" }
  if($low -like "*\pixel\*"){ return "ps" }
  return $null
}
function Sniff-StageFromSource([string]$path){
  $txt = Get-Content -LiteralPath $path -TotalCount 200 -Raw -ErrorAction SilentlyContinue
  if(-not $txt){ return $null }
  if($txt -match '\[\s*numthreads\s*\('){ return "cs" }
  if($txt -match 'SV_Position' -and $txt -match '\bVS(Main|Entry|Vertex)'){ return "vs" }
  if($txt -match 'SV_Target'   -or  $txt -match '\bPS(Main|Entry|Pixel)'){ return "ps" }
  return $null
}
function Entry-Candidates([string]$stage){
  switch($stage){
    "cs" { return @("CSMain","main","ComputeMain","MainCS") }
    "vs" { return @("VSMain","main","VertexMain","MainVS") }
    "ps" { return @("PSMain","main","PixelMain","MainPS") }
    "gs" { return @("GSMain","main") }
    "ds" { return @("DSMain","main") }
    "hs" { return @("HSMain","main") }
    default { return @("main") }
  }
}

#----------------------------- Sidecar JSON (.hlsl.json) -----------------------
function Load-Sidecar([string]$hlslPath){
  $jsonPath = "$hlslPath.json"
  if(Test-Path $jsonPath){
    try { return (Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json) }
    catch { Write-Warn2 "Invalid sidecar JSON: $jsonPath — $_" }
  }
  return $null
}

#----------------------------- Permutation helpers -----------------------------
function Parse-PermutationString([string]$s){
  if(-not $s){ return @() }
  $parts = $s -split '[;,]' | Where-Object { $_ -and $_.Trim() -ne "" }
  return @($parts | ForEach-Object { $_.Trim() })
}
function Merge-DefinesOrdered([string[]]$a,[string[]]$b){
  $all = @($a + $b | Where-Object { $_ -and $_ -ne "" })
  # canonicalize order (for stable hashing)
  return @($all | Sort-Object -Unique)
}

#----------------------------- Hashing & includes ------------------------------
function Hash-FNV64([byte[]]$bytes){
  [UInt64]$hash = 1469598103934665603
  [UInt64]$prime = 1099511628211
  foreach($b in $bytes){ $hash = ($hash -bxor [uint64]$b) * $prime }
  return ("{0:x16}" -f $hash)
}
function Hash-FileFNV64([string]$path){
  $fs=[System.IO.File]::Open($path,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite)
  try{
    $buf = New-Object byte[] 65536
    [UInt64]$hash = 1469598103934665603
    [UInt64]$prime = 1099511628211
    while(($read=$fs.Read($buf,0,$buf.Length)) -gt 0){
      for($i=0;$i -lt $read;$i++){ $hash = ($hash -bxor [uint64]$buf[$i]) * $prime }
    }
    return ("{0:x16}" -f $hash)
  } finally { $fs.Close() }
}
function Get-IncludesRecursive([string]$file,[hashtable]$cache){
  if($cache.ContainsKey($file)){ return $cache[$file] }
  $incs = New-Object System.Collections.Generic.HashSet[string]
  $dir  = Split-Path -Parent $file
  $txt  = Get-Content -LiteralPath $file -Raw -ErrorAction SilentlyContinue
  if($txt){
    foreach($m in ([regex]::Matches($txt,'^\s*#\s*include\s*[<"]([^">]+)[">]','Multiline'))){
      $inc=$m.Groups[1].Value
      $resolved=$null
      $c1=Join-Path $dir $inc; if(Test-Path $c1){ $resolved=(Resolve-Path $c1).ProviderPath } else {
        foreach($id in $IncludeDirs){ $cand=Join-Path $id $inc; if(Test-Path $cand){ $resolved=(Resolve-Path $cand).ProviderPath; break } }
      }
      if($resolved){
        [void]$incs.Add($resolved)
        foreach($ch in (Get-IncludesRecursive $resolved $cache)){ [void]$incs.Add($ch) }
      }
    }
  }
  $cache[$file]=$incs
  return $incs
}
function Build-DepGraph($sources,$includeCache){
  $map=@{}
  foreach($s in $sources){
    $incs = Get-IncludesRecursive $s.FullName $includeCache
    foreach($i in $incs){
      if(-not $map.ContainsKey($i)){ $map[$i]=New-Object System.Collections.Generic.HashSet[string] }
      [void]$map[$i].Add($s.FullName)
    }
  }
  return $map
}
function Compute-InputsHash($src,$includes,$defines,$profile,$entry,$compiler,$config){
  $acc = New-Object System.Collections.Generic.List[string]
  $acc.Add((Hash-FileFNV64 $src))
  foreach($i in $includes){ $acc.Add((Hash-FileFNV64 $i)) }
  $acc.Add([string]::Join(";", $defines))
  $acc.Add($profile); $acc.Add($entry); $acc.Add($compiler); $acc.Add($config)
  $bytes=[System.Text.Encoding]::UTF8.GetBytes([string]::Join("|",$acc))
  return (Hash-FNV64 $bytes)
}

#----------------------------- Compiler args -----------------------------------
function Build-FXC-Args($profile,$entry,$src,$out,$pdb,$asm,$hdr,$defs){
  $args=@("/nologo","/T",$profile,"/E",$entry,"/Fo",$out)
  foreach($d in $defs){ $args += @("/D",$d) }
  foreach($i in $IncludeDirs){ $args += @("/I",$i) }
  if($Config -eq "Debug"){
    $args += @("/Zi"); if($pdb){ $args += @("/Fd",$pdb) }; $args += @("/Od")
  } else {
    $args += @("/O3","/Qstrip_debug","/Qstrip_reflect"); if($WarningsAsErrors){ $args += @("/WX") }
  }
  if($DisableValidation){ $args += @("/Vd") }
  if($asm){ $args += @("/Fc",$asm) }
  if($hdr){ $args += @("/Fh",$hdr) }
  return $args
}
function Build-DXC-Args($profile,$entry,$src,$out,$pdb,$asm,$defs){
  $args=@("-nologo","-T",$profile,"-E",$entry,$src,"-Fo",$out)
  foreach($d in $defs){ $args += @("-D",$d) }
  foreach($i in $IncludeDirs){ $args += @("-I",$i) }
  if($Config -eq "Debug"){
    $args += @("-Zi"); if($EmbedDebug){ $args += @("-Qembed_debug") }; $args += @("-Od")
  } else {
    $args += @("-O3","-Qstrip_debug","-Qstrip_reflect")
  }
  if($DisableValidation){ $args += @("-Vd") }
  if($pdb){ $args += @("-Fd",$pdb) }
  if($asm){ $args += @("-Fc",$asm) }
  return $args
}

#----------------------------- Job planning ------------------------------------
function Plan-Jobs($files){
  $jobs=@()
  foreach($f in $files){
    if($f.Name -like "*.hlsli"){ continue }
    $src=$f.FullName
    $hints = Read-HeaderHints $src
    $stage = $hints.stage; if(-not $stage){ $stage = Guess-StageFromName $src }; if(-not $stage){ $stage = Sniff-StageFromSource $src }

    $sidecar = Load-Sidecar $src
    $globalPerms = @()
    foreach($p in $Permutations){ $globalPerms += ,(Parse-PermutationString $p) }

    if($sidecar -and $sidecar.stages){
      foreach($st in $sidecar.stages){
        $stg = if($st.stage){ $st.stage } else { $stage }
        if(-not $stg){ Write-Warn2 "Skipping (unknown stage): $($f.Name)"; continue }
        $pro = if($st.profile){ $st.profile } else { Stage-Profile $stg }
        $localEntries = if($st.entries){ @($st.entries) } elseif($hints.entries.Count -gt 0){ $hints.entries } else { ,((Entry-Candidates $stg)[0]) }
        $localDefines = Merge-DefinesOrdered $Defines $st.defines
        $permutes = @()
        if($st.permutations){
          foreach($pp in $st.permutations){ $permutes += ,(Parse-PermutationString ([string]$pp)) }
        }
        if($globalPerms.Count -eq 0 -and $permutes.Count -eq 0){
          foreach($e in $localEntries){
            $jobs += [PSCustomObject]@{ source=$src; stage=$stg; entry=$e; profile=$pro; defines=$localDefines; permSig="" }
          }
        } else {
          $permUnion = if($permutes.Count -gt 0){ $permutes } else { @(@()) }
          if($globalPerms.Count -gt 0){
            # combine global x local permutations
            $tmp=@()
            foreach($g in $globalPerms){
              if($permUnion.Count -gt 0){
                foreach($l in $permUnion){ $tmp += ,(Merge-DefinesOrdered $g $l) }
              } else { $tmp += ,$g }
            }
            if($tmp.Count -gt 0){ $permUnion = $tmp }
          }
          if($permUnion.Count -eq 0){ $permUnion = @(@()) }
          foreach($e in $localEntries){
            foreach($perm in $permUnion){
              $defs = Merge-DefinesOrdered $localDefines $perm
              $permSig = if($perm.Count -gt 0){ [string]::Join(";", $perm) } else { "" }
              $jobs += [PSCustomObject]@{ source=$src; stage=$stg; entry=$e; profile=$pro; defines=$defs; permSig=$permSig }
            }
          }
        }
      }
      continue
    }

    if(-not $stage){ Write-Info "Skipping (stage unknown): $($f.Name)"; continue }
    $profile = if($hints.profile){ $hints.profile } else { Stage-Profile $stage }
    $entries = if($hints.entries.Count -gt 0){ $hints.entries } else { ,((Entry-Candidates $stage)[0]) }
    $localDefines = $Defines

    $permUnion = if($Permutations.Count -gt 0){ @() } else { @(@()) }
    foreach($p in $Permutations){ $permUnion += ,(Parse-PermutationString $p) }
    if($permUnion.Count -eq 0){ $permUnion = @(@()) }

    foreach($e in $entries){
      foreach($perm in $permUnion){
        $defs = Merge-DefinesOrdered $localDefines $perm
        $permSig = if($perm.Count -gt 0){ [string]::Join(";", $perm) } else { "" }
        $jobs += [PSCustomObject]@{ source=$src; stage=$stage; entry=$e; profile=$profile; defines=$defs; permSig=$permSig }
      }
    }
  }
  return $jobs
}

#----------------------------- Incremental & filenames -------------------------
function Build-JobStem($src,$stage,$entry,$defines){
  $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
  foreach($suf in $StageSuffixes.Keys){ if($src.ToLowerInvariant().EndsWith($suf)){ $base = [System.IO.Path]::GetFileNameWithoutExtension($src.Substring(0,$src.Length-$suf.Length+6)) } }
  $defStr = if($defines.Count -gt 0){ [string]::Join(";", $defines) } else { "" }
  $sig = if($defStr -ne ""){ (Hash-FNV64 ([System.Text.Encoding]::UTF8.GetBytes($defStr))).Substring(0,8) } else { "0" }
  return "{0}.{1}.{2}.{3}" -f $base,$stage,$entry,$sig
}
function Needs-Rebuild($src,$dst,$includes,$job,$compilerName){
  if($Force){ return $true }
  if(-not (Test-Path $dst)){ return $true }
  if($DeterministicHash){
    $cacheFile = Join-Path $CacheDir ("{0}.hash" -f (Hash-FNV64 ([System.Text.Encoding]::UTF8.GetBytes(($job.source + "|" + $job.stage + "|" + $job.entry + "|" + $job.profile + "|" + [string]::Join(";",$job.defines))))))
    $now = Compute-InputsHash $src $includes $job.defines $job.profile $job.entry $compilerName $Config
    if(Test-Path $cacheFile){
      $prev = (Get-Content -LiteralPath $cacheFile -Raw -ErrorAction SilentlyContinue)
      return $prev -ne $now
    }
    return $true
  } else {
    $outTime = (Get-Item -LiteralPath $dst).LastWriteTimeUtc
    if((Get-Item -LiteralPath $src).LastWriteTimeUtc -gt $outTime){ return $true }
    foreach($i in $includes){ if((Get-Item -LiteralPath $i).LastWriteTimeUtc -gt $outTime){ return $true } }
    return $false
  }
}
function Update-CacheHash($job,$includes,$compilerName){
  if(-not $DeterministicHash){ return }
  $cacheFile = Join-Path $CacheDir ("{0}.hash" -f (Hash-FNV64 ([System.Text.Encoding]::UTF8.GetBytes(($job.source + "|" + $job.stage + "|" + $job.entry + "|" + $job.profile + "|" + [string]::Join(";",$job.defines))))))
  $now = Compute-InputsHash $job.source $includes $job.defines $job.profile $job.entry $compilerName $Config
  [System.IO.File]::WriteAllText($cacheFile,$now,[System.Text.Encoding]::UTF8)
}

#----------------------------- Process orchestration ---------------------------
class RunningProc {
  [System.Diagnostics.Process]$Proc
  [string]$LogPath
  [hashtable]$Row
  RunningProc([System.Diagnostics.Process]$p,[string]$l,[hashtable]$r){ $this.Proc=$p; $this.LogPath=$l; $this.Row=$r }
}
function Start-FXC($row){
  $j=$row.job
  $args = Build-FXC-Args $j.profile $j.entry $j.source $row.outFile $row.pdbFile $row.asmFile $row.hdrFile $j.defines
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=$FXC; $psi.Arguments=($args -join ' ')
  $psi.RedirectStandardError=$true; $psi.RedirectStandardOutput=$true; $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
  $p=[System.Diagnostics.Process]::Start($psi); $p.EnableRaisingEvents=$true; return $p
}
function Start-DXC($row){
  $j=$row.job
  $args = Build-DXC-Args $j.profile $j.entry $j.source $row.outFile $row.pdbFile $row.asmFile $j.defines
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=$DXC; $psi.Arguments=($args -join ' ')
  $psi.RedirectStandardError=$true; $psi.RedirectStandardOutput=$true; $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
  $p=[System.Diagnostics.Process]::Start($psi); $p.EnableRaisingEvents=$true; return $p
}
function Pump-Parallel($plan,$compilerName){
  $running = New-Object System.Collections.Generic.List[RunningProc]
  $q = New-Object System.Collections.Generic.Queue[hashtable]
  foreach($r in $plan){ $q.Enqueue($r) }

  while($q.Count -gt 0 -or $running.Count -gt 0){
    while(($running.Count -lt $MaxJobs) -and ($q.Count -gt 0)){
      $row=$q.Dequeue()
      Write-Info ("Compiling {0} [{1}/{2}/{3}] -> {4}" -f (Split-Path -Leaf $row.job.source),$row.job.stage,$row.job.profile,$row.job.entry,(Split-Path -Leaf $row.outFile))
      $p = if($compilerName -eq "FXC"){ Start-FXC $row } else { Start-DXC $row }
      $running.Add([RunningProc]::new($p,$row.logFile,$row))
    }
    for($i=$running.Count-1; $i -ge 0; $i--){
      $rp=$running[$i]
      if($rp.Proc.HasExited){
        $stdout=$rp.Proc.StandardOutput.ReadToEnd(); $stderr=$rp.Proc.StandardError.ReadToEnd()
        if(-not [string]::IsNullOrWhiteSpace($stdout)){ [System.IO.File]::WriteAllText($rp.LogPath,$stdout,[System.Text.Encoding]::UTF8) }
        if(-not [string]::IsNullOrWhiteSpace($stderr)){ Add-Content -LiteralPath $rp.LogPath -Value $stderr }
        $exit=$rp.Proc.ExitCode; $running.RemoveAt($i)
        if($exit -eq 0){
          $rp.Row.success=$true; $rp.Row.entry=$rp.Row.job.entry
          $rp.Row.elapsedMs=(New-TimeSpan -Start $rp.Row.start -End (Get-Date)).TotalMilliseconds
          $rp.Row.bytesOut = (Test-Path $rp.Row.outFile) ? (Get-Item $rp.Row.outFile).Length : 0
          Update-CacheHash $rp.Row.job $rp.Row.includes $compilerName
          Write-Ok ("✓ {0} :: {1}/{2} [{3:N0} ms]" -f (Split-Path -Leaf $rp.Row.job.source),$rp.Row.job.stage,$rp.Row.job.entry,$rp.Row.elapsedMs)
        } else {
          $rp.Row.success=$false; $rp.Row.message="Compilation failed. See log: $($rp.LogPath)"
          Write-Err2 ("✗ FAILED {0} :: {1}/{2} (see {3})" -f (Split-Path -Leaf $rp.Row.job.source),$rp.Row.job.stage,$rp.Row.job.entry,$rp.LogPath)
        }
      }
    }
    if($running.Count -gt 0){ Start-Sleep -Milliseconds 30 }
  }
}

#----------------------------- Discover sources & plan -------------------------
$allFiles = Get-ChildItem -LiteralPath $ShaderDir -Recurse -Include *.hlsl -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike "*.hlsli" }
if($allFiles.Count -eq 0){ Write-Warn2 "No .hlsl under $ShaderDir" }

$includeCache = @{}
$depGraph     = Build-DepGraph $allFiles $includeCache
$jobs         = Plan-Jobs $allFiles

$manifest = [ordered]@{
  version=3
  game="Colony-Game"
  config=$Config
  compiler=(if($UseDXC){"DXC"}else{"FXC"})
  compilerPath=(if($UseDXC){$DXC}else{$FXC})
  shaderRoot=$ShaderDir
  outputRoot=$OutDir
  generatedAtUTC=[DateTime]::UtcNow.ToString("o")
  warningsAsErrors=[bool]$WarningsAsErrors
  deterministicHash=[bool]$DeterministicHash
  emitDisasm=[bool]$EmitDisasm
  emitHeaders=[bool]$EmitHeaders
  permutations=$Permutations
  shaders=@()
  stats=@{ planned=0; compiled=0; skipped=0; failed=0; msTotal=0 }
}

$plan = New-Object System.Collections.Generic.List[hashtable]
$skipped=0
foreach($j in $jobs){
  $incs = Get-IncludesRecursive $j.source $includeCache
  $stem = Build-JobStem $j.source $j.stage $j.entry $j.defines
  $outFile = Join-Path $OutCSO  "$stem.cso"
  $pdbFile = if($Config -eq "Debug"){ Join-Path $OutPDB "$stem.pdb" } else { $null }
  $asmFile = if($EmitDisasm){ Join-Path $OutASM "$stem.asm" } else { $null }
  $hdrFile = if($EmitHeaders -and -not $UseDXC){ Join-Path $OutHDR "$stem.h" } else { $null }

  $need = Needs-Rebuild $j.source $outFile $incs $j (if($UseDXC){"DXC"}else{"FXC"})
  $depFile = Join-Path $OutDEP "$stem.d"
  $logFile = Join-Path $OutLOG "$stem.log"
  $depLine = "$outFile : $($j.source) " + ([string]::Join(" ",$incs))
  [System.IO.File]::WriteAllText($depFile,$depLine,[System.Text.Encoding]::UTF8)

  $row = @{
    job=$j; includes=$incs; outFile=$outFile; pdbFile=$pdbFile; asmFile=$asmFile; hdrFile=$hdrFile
    logFile=$logFile; depFile=$depFile; start=(Get-Date)
    success=$false; entry=$null; elapsedMs=0; bytesOut=0; message=""
  }

  if(-not $need){
    $row.success=$true; $row.message="Up-to-date"; $skipped++
  } else {
    if($ListOnly){ Write-Info ("→ Would compile {0} [{1}/{2}] ({3})" -f (Split-Path -Leaf $j.source),$j.stage,$j.entry,$stem) }
    else         { $plan.Add($row) }
  }

  $manifest.shaders += [ordered]@{
    id=(Hash-FNV64 ([System.Text.Encoding]::UTF8.GetBytes("$($j.source)|$($j.stage)|$($j.entry)|$($j.profile)|$([string]::Join(';',$j.defines))")))
    source=$j.source; stage=$j.stage; entry=$j.entry; profile=$j.profile
    defines=$j.defines; permSignature=$j.permSig
    output=$outFile; pdb=$pdbFile; asm=$asmFile; header=$hdrFile
    includes=$incs.ToArray(); upToDate=(-not $need)
  }
}

$manifest.stats.planned = $manifest.shaders.Count
$manifest.stats.skipped = $skipped

if($ListOnly){
  $manifestJson = $manifest | ConvertTo-Json -Depth 10
  [System.IO.File]::WriteAllText($ManifestPath,$manifestJson,[System.Text.Encoding]::UTF8)
  Write-Ok "Dry-run manifest written: $ManifestPath"
  exit 0
}

if($plan.Count -gt 0){
  Write-Info ("Compiling {0} job(s) with up to {1} worker(s)..." -f $plan.Count,$MaxJobs)
  $t0=Get-Date
  Pump-Parallel $plan (if($UseDXC){"DXC"}else{"FXC"})
  $manifest.stats.msTotal = (New-TimeSpan -Start $t0 -End (Get-Date)).TotalMilliseconds
} else {
  Write-Info "Everything is up-to-date."
}

# refresh results back into manifest
$failed=0; $compiled=0
foreach($row in $plan){
  $m = $manifest.shaders | Where-Object { $_.output -eq $row.outFile }
  if($m){
    $m.success  = $row.success
    $m.entry    = $row.entry
    $m.elapsedMs= [math]::Round($row.elapsedMs)
    $m.bytesOut = $row.bytesOut
    if(-not $row.success){ $m.message = $row.message }
    if($row.success){ $compiled++ } else { $failed++ }
  }
}
$manifest.stats.compiled = $compiled
$manifest.stats.failed   = $failed

# Write manifest
$manifestJson = $manifest | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($ManifestPath,$manifestJson,[System.Text.Encoding]::UTF8)
Write-Ok "Manifest: $ManifestPath"

#----------------------------- Packaging (optional) ----------------------------
function Pack-Zip([string]$zipPath,[System.Collections.ArrayList]$files,[string]$root){
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  if(Test-Path $zipPath){ Remove-Item -LiteralPath $zipPath -Force }
  $zip = [System.IO.Compression.ZipArchive]::new(
    [System.IO.File]::Open($zipPath,[System.IO.FileMode]::Create),
    [System.IO.Compression.ZipArchiveMode]::Create,$false,[System.Text.Encoding]::UTF8)
  try{
    foreach($f in $files){
      $rel = [IO.Path]::GetRelativePath($root, $f)
      $entry = $zip.CreateEntry($rel, [System.IO.Compression.CompressionLevel]::Optimal)
      $in = [System.IO.File]::OpenRead($f)
      try{
        $outStream = $entry.Open()
        $in.CopyTo($outStream)
        $outStream.Close()
      } finally { $in.Close() }
    }
  } finally { $zip.Dispose() }
}
function Pack-Pak([string]$pakPath,[System.Collections.ArrayList]$files,[string]$root){
  # Simple custom format:
  # magic(8): 'CNPACK01'
  # u32 count
  # [count x { u32 nameLen, bytes name (UTF8), u64 offset, u32 size }]
  # [file blobs ...]
  $fs = [System.IO.File]::Open($pakPath,[System.IO.FileMode]::Create,[System.IO.FileAccess]::ReadWrite,[System.IO.FileShare]::None)
  try{
    $bw = New-Object System.IO.BinaryWriter($fs,[System.Text.Encoding]::UTF8)
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes("CNPACK01"))
    $bw.Write([int]$files.Count)
    $tablePos = $fs.Position
    # temp table
    $entries=@()
    foreach($f in $files){
      $rel = [IO.Path]::GetRelativePath($root,$f).Replace("\","/")
      $bytes = [System.Text.Encoding]::UTF8.GetBytes($rel)
      $bw.Write([int]$bytes.Length) ; $bw.Write($bytes)
      $bw.Write([UInt64]0)          # placeholder offset
      $bw.Write([int]0)             # placeholder size
      $entries += ,@($rel,$bytes.Length)
    }
    # write blobs & backpatch
    $blobPositions=@()
    for($i=0;$i -lt $files.Count;$i++){
      $pos = $fs.Position
      $blobPositions += ,$pos
      $data = [System.IO.File]::ReadAllBytes($files[$i])
      $bw.Write($data)
    }
    # backpatch
    $fs.Position = 8 + 4 # after header
    for($i=0;$i -lt $files.Count;$i++){
      $nameLen = $entries[$i][1]
      $fs.Position += 4 + $nameLen
      $bw.Write([UInt64]$blobPositions[$i])
      $size = (Get-Item $files[$i]).Length
      $bw.Write([int]$size)
    }
    $bw.Flush()
  } finally { $fs.Close() }
}
if($Pack){
  $compiledFiles = New-Object System.Collections.ArrayList
  foreach($row in $manifest.shaders){
    if($row.success -or $row.upToDate){ [void]$compiledFiles.Add($row.output) }
  }
  # stable order by relative path + filename
  $sorted = @($compiledFiles | Sort-Object { [IO.Path]::GetFileName($_) })
  $pkgStem = Join-Path $PkgDir ($PackName + (if($Config -eq "Debug"){"_Debug"}{"_Release"}))
  if($PackType -eq "zip"){
    $zip = $pkgStem + ".zip"
    Pack-Zip $zip $sorted $OutDir
    Write-Ok "Packaged ZIP: $zip"
  } else {
    $pak = $pkgStem + ".pak"
    Pack-Pak $pak $sorted $OutDir
    Write-Ok "Packaged PAK: $pak"
  }
}

#----------------------------- Watch mode --------------------------------------
if($Watch){
  Write-Info "Watch: monitoring .hlsl/.hlsli/.hlsl.json under $ShaderDir (Ctrl+C to stop)"
  $fsw1 = New-Object System.IO.FileSystemWatcher
  $fsw1.Path = $ShaderDir; $fsw1.Filter="*.hlsl";  $fsw1.IncludeSubdirectories=$true; $fsw1.EnableRaisingEvents=$true
  $fsw2 = New-Object System.IO.FileSystemWatcher
  $fsw2.Path = $ShaderDir; $fsw2.Filter="*.hlsli"; $fsw2.IncludeSubdirectories=$true; $fsw2.EnableRaisingEvents=$true
  $fsw3 = New-Object System.IO.FileSystemWatcher
  $fsw3.Path = $ShaderDir; $fsw3.Filter="*.hlsl.json"; $fsw3.IncludeSubdirectories=$true; $fsw3.EnableRaisingEvents=$true

  $pending = New-Object System.Collections.Generic.HashSet[string]
  $timer = New-Object System.Timers.Timer
  $timer.Interval=250; $timer.AutoReset=$true

  $onFs = { param($s,$e) if($e.FullPath){ [void]$pending.Add($e.FullPath) } }
  $onTick = {
    if($pending.Count -eq 0){ return }
    $batch=@($pending); $pending.Clear() | Out-Null
    $targets = New-Object System.Collections.Generic.HashSet[string]
    foreach($p in $batch){
      $low=$p.ToLowerInvariant()
      if($low.EndsWith(".hlsl")){ [void]$targets.Add($p) }
      elseif($low.EndsWith(".hlsli")){ if($depGraph.ContainsKey($p)){ foreach($parent in $depGraph[$p]){ [void]$targets.Add($parent) } } }
      elseif($low.EndsWith(".hlsl.json")){
        $src = $p.Substring(0,$p.Length-5) # strip ".json"
        if(Test-Path $src){ [void]$targets.Add($src) }
      }
    }
    if($targets.Count -eq 0){ return }
    Write-Info ("Change detected: " + ([string]::Join(", ", ($batch | ForEach-Object { Split-Path -Leaf $_ }))))
    $subset = $allFiles | Where-Object { $targets.Contains($_.FullName) }
    foreach($s in $subset){ $includeCache.Remove($s.FullName) | Out-Null }
    $jobs2   = Plan-Jobs $subset
    $plan2   = New-Object System.Collections.Generic.List[hashtable]
    foreach($j in $jobs2){
      $incs = Get-IncludesRecursive $j.source $includeCache
      $stem = Build-JobStem $j.source $j.stage $j.entry $j.defines
      $outFile = Join-Path $OutCSO  "$stem.cso"
      $pdbFile = if($Config -eq "Debug"){ Join-Path $OutPDB "$stem.pdb" } else { $null }
      $asmFile = if($EmitDisasm){ Join-Path $OutASM "$stem.asm" } else { $null }
      $hdrFile = if($EmitHeaders -and -not $UseDXC){ Join-Path $OutHDR "$stem.h" } else { $null }
      $plan2.Add(@{
        job=$j; includes=$incs; outFile=$outFile; pdbFile=$pdbFile; asmFile=$asmFile; hdrFile=$hdrFile
        logFile=(Join-Path $OutLOG "$stem.log"); depFile=(Join-Path $OutDEP "$stem.d"); start=(Get-Date)
        success=$false; entry=$null; elapsedMs=0; bytesOut=0; message=""
      })
      $depLine = "$outFile : $($j.source) " + ([string]::Join(" ",$incs))
      [System.IO.File]::WriteAllText((Join-Path $OutDEP "$stem.d"),$depLine,[System.Text.Encoding]::UTF8)
    }
    if($plan2.Count -gt 0){
      Pump-Parallel $plan2 (if($UseDXC){"DXC"}else{"FXC"})
      $depGraph = Build-DepGraph $allFiles $includeCache
      Write-Ok "Rebuild complete."
    }
  }

  $eh1=Register-ObjectEvent $fsw1 Changed -Action $onFs
  $eh2=Register-ObjectEvent $fsw1 Created -Action $onFs
  $eh3=Register-ObjectEvent $fsw1 Renamed -Action $onFs
  $eh4=Register-ObjectEvent $fsw2 Changed -Action $onFs
  $eh5=Register-ObjectEvent $fsw2 Created -Action $onFs
  $eh6=Register-ObjectEvent $fsw2 Renamed -Action $onFs
  $eh7=Register-ObjectEvent $fsw3 Changed -Action $onFs
  $eh8=Register-ObjectEvent $fsw3 Created -Action $onFs
  $eh9=Register-ObjectEvent $fsw3 Renamed -Action $onFs
  $tick=Register-ObjectEvent $timer Elapsed -Action $onTick
  $timer.Start()
  try{ while($true){ Start-Sleep -Seconds 1 } }
  finally{
    $timer.Stop()
    foreach($h in @($eh1,$eh2,$eh3,$eh4,$eh5,$eh6,$eh7,$eh8,$eh9,$tick)){ if($h){ $h | Unregister-Event } }
    $fsw1.EnableRaisingEvents=$false; $fsw2.EnableRaisingEvents=$false; $fsw3.EnableRaisingEvents=$false
  }
}

#----------------------------- Exit code ---------------------------------------
$fail = ($manifest.shaders | Where-Object { $_.success -eq $false -and $_.upToDate -ne $true }).Count
if($fail -gt 0){ exit 1 } else { exit 0 }
