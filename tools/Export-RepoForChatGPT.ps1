<#
Export-RepoForChatGPT.ps1

Creates an "AI bundle" (chunked text files) that you can upload to ChatGPT for repo searching
WITHOUT using the GitHub connector.

Default behavior is privacy-minded:
- Uses `git ls-files` (tracked files only) when possible
- Skips common build/artifact folders
- Skips likely-secret files (.env, *.pem, *.pfx, etc.)
- Text-only (binary detection by null-byte)
- Size-limits per file + chunking per bundle part
#>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $false)]
  [string]$RepoPath = (Get-Location).Path,

  [Parameter(Mandatory = $false)]
  [string]$OutDir,

  [Parameter(Mandatory = $false)]
  [switch]$TrackedOnly = $true,

  [Parameter(Mandatory = $false)]
  [int]$MaxFileMB = 2,

  [Parameter(Mandatory = $false)]
  [int]$ChunkMB = 20,

  [Parameter(Mandatory = $false)]
  [switch]$MakeZip,

  [Parameter(Mandatory = $false)]
  [switch]$RedactSecrets,

  [Parameter(Mandatory = $false)]
  [string[]]$ExcludeGlobs = @(
    # folders
    ".git/*",".vs/*","**/.vs/*",
    "build/*","**/build/*",
    "out/*","**/out/*",
    "bin/*","**/bin/*",
    "obj/*","**/obj/*",
    "x64/*","**/x64/*",
    "x86/*","**/x86/*",
    "Debug/*","**/Debug/*",
    "Release/*","**/Release/*",
    "vcpkg_installed/*","**/vcpkg_installed/*",
    "CMakeFiles/*","**/CMakeFiles/*",
    ".idea/*",".vscode/*",

    # big/binary-ish types
    "*.png","*.jpg","*.jpeg","*.webp","*.gif","*.bmp","*.ico",
    "*.zip","*.7z","*.rar","*.tar","*.gz",
    "*.exe","*.dll","*.pdb","*.lib","*.obj","*.ilk","*.exp",
    "*.mp4","*.mov","*.mkv","*.wav","*.mp3","*.ogg",
    "*.pdf"
  ),

  [Parameter(Mandatory = $false)]
  [string[]]$ExcludeNameGlobs = @(
    ".env",".env.*",
    "*secrets*","*secret*","*private*",
    "*.pem","*.pfx","*.p12","*.key","id_rsa","id_ed25519",
    "*.kdbx"
  ),

  [Parameter(Mandatory = $false)]
  [string[]]$IncludeExtensions = @(
    # code
    ".c",".cc",".cpp",".cxx",".h",".hh",".hpp",".hxx",".inl",".ixx",
    ".cs",".py",".js",".ts",".jsx",".tsx",".java",".go",".rs",
    # build/config/docs
    ".cmake",".txt",".md",".rst",".json",".yml",".yaml",".toml",".ini",".cfg",
    ".props",".targets",".sln",".vcxproj",".filters",".natvis",
    # shaders
    ".hlsl",".glsl",".shader"
  )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-RelPath([string]$p) {
  return ($p -replace "\\","/").TrimStart("./")
}

function Get-RelativePath([string]$basePath, [string]$fullPath) {
  $basePath = (Resolve-Path $basePath).Path
  $fullPath = (Resolve-Path $fullPath).Path
  $baseUri = New-Object System.Uri(($basePath.TrimEnd('\') + '\'))
  $fullUri = New-Object System.Uri($fullPath)
  $rel = $baseUri.MakeRelativeUri($fullUri).ToString()
  return [System.Uri]::UnescapeDataString($rel) -replace "/", "\"
}

function Matches-AnyGlob([string]$relNorm, [string[]]$globs) {
  foreach ($g in $globs) {
    $pat = Normalize-RelPath $g
    if ($relNorm -like $pat) { return $true }
    # support "**/" by also checking without leading directories
    if ($pat -like "**/*") {
      $tail = $pat.Substring(3)
      if ($relNorm -like $tail) { return $true }
    }
  }
  return $false
}

function Has-IncludeExtension([string]$path, [string[]]$exts) {
  $leaf = [System.IO.Path]::GetFileName($path)
  if ($leaf -ieq "CMakeLists.txt") { return $true }
  $ext = [System.IO.Path]::GetExtension($path)
  if ([string]::IsNullOrWhiteSpace($ext)) { return $false }
  return $exts -icontains $ext
}

function Read-FileBytes([string]$path) {
  return [System.IO.File]::ReadAllBytes($path)
}

function Bytes-LookLikeBinary([byte[]]$bytes) {
  # crude but effective: null byte indicates binary for our purposes
  foreach ($b in $bytes) { if ($b -eq 0) { return $true } }
  return $false
}

function Decode-BytesToText([byte[]]$bytes) {
  # UTF8 decode (BOM handled), replacement chars for invalid sequences
  return [System.Text.Encoding]::UTF8.GetString($bytes)
}

function Redact-Text([string]$text) {
  if (-not $RedactSecrets) { return $text }

  # Redact private key blocks
  $text = [regex]::Replace(
    $text,
    "-----BEGIN[\s\S]{0,40}PRIVATE KEY-----[\s\S]*?-----END[\s\S]{0,40}PRIVATE KEY-----",
    "[REDACTED_PRIVATE_KEY_BLOCK]",
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
  )

  # Redact simple key/value secrets (may over-redact; review output!)
  $text = [regex]::Replace(
    $text,
    "(?im)^(?<k>\s*(password|passwd|secret|api[_-]?key|token|access[_-]?key|client[_-]?secret)\s*[:=]\s*)(?<v>.+?)\s*$",
    '${k}[REDACTED]'
  )

  # Redact AWS access key IDs (common pattern)
  $text = [regex]::Replace($text, "\b(AKIA|ASIA)[0-9A-Z]{16}\b", "[REDACTED_AWS_ACCESS_KEY_ID]")

  return $text
}

function New-Utf8Writer([string]$path) {
  $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
  return New-Object System.IO.StreamWriter($path, $false, $utf8NoBom)
}

# Resolve paths
$RepoPath = (Resolve-Path $RepoPath).Path
if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = Join-Path $RepoPath "_chatgpt_export"
}
$OutDir = (Resolve-Path (New-Item -ItemType Directory -Force -Path $OutDir)).Path

$maxBytesPerFile = $MaxFileMB * 1MB
$chunkBytes = $ChunkMB * 1MB

Write-Host "RepoPath: $RepoPath"
Write-Host "OutDir  : $OutDir"
Write-Host "TrackedOnly: $TrackedOnly  MaxFileMB: $MaxFileMB  ChunkMB: $ChunkMB  RedactSecrets: $RedactSecrets"
Write-Host ""

# Gather files
$files = @()

$git = Get-Command git -ErrorAction SilentlyContinue
if ($TrackedOnly -and $git) {
  Write-Host "Using git ls-files (tracked files only)..."
  $tracked = & git -C $RepoPath ls-files
  foreach ($rel in $tracked) {
    $full = Join-Path $RepoPath $rel
    if (Test-Path $full -PathType Leaf) {
      $files += (Resolve-Path $full).Path
    }
  }
} else {
  Write-Host "Using filesystem scan (Get-ChildItem)..."
  $files = Get-ChildItem -Path $RepoPath -Recurse -File | ForEach-Object { $_.FullName }
}

# Filter files
$selected = New-Object System.Collections.Generic.List[object]
foreach ($full in $files) {
  # exclude by name globs (leaf)
  $leaf = [System.IO.Path]::GetFileName($full)
  $leafNorm = Normalize-RelPath $leaf
  if (Matches-AnyGlob $leafNorm $ExcludeNameGlobs) { continue }

  $rel = Get-RelativePath $RepoPath $full
  $relNorm = Normalize-RelPath $rel

  if (Matches-AnyGlob $relNorm $ExcludeGlobs) { continue }
  if (-not (Has-IncludeExtension $full $IncludeExtensions)) { continue }

  $fi = Get-Item $full
  if ($fi.Length -gt $maxBytesPerFile) { continue }

  $selected.Add([pscustomobject]@{
    FullPath = $full
    RelPath  = $relNorm
    Bytes    = [int64]$fi.Length
    Modified = $fi.LastWriteTimeUtc
  }) | Out-Null
}

$selected = $selected | Sort-Object RelPath
Write-Host ("Selected files: {0}" -f $selected.Count)

# Outputs
$fileListTxt = Join-Path $OutDir "REPO_FILE_LIST.txt"
$fileListCsv = Join-Path $OutDir "REPO_FILE_LIST.csv"
$manifest    = Join-Path $OutDir "MANIFEST.json"
$promptGuide = Join-Path $OutDir "CHATGPT_PROMPT.txt"

# File list
$selected | ForEach-Object {
  "{0}`t{1}`t{2}" -f $_.RelPath, $_.Bytes, $_.Modified.ToString("o")
} | Set-Content -Path $fileListTxt -Encoding utf8

$selected | Export-Csv -Path $fileListCsv -NoTypeInformation -Encoding utf8

# Prepare bundle writers
$part = 1
$currentBytes = 0
$bundlePaths = New-Object System.Collections.Generic.List[string]

function Open-NewBundlePart {
  param([int]$partNumber)
  $p = Join-Path $OutDir ("BUNDLE_part{0:000}.txt" -f $partNumber)
  $bundlePaths.Add($p) | Out-Null
  $w = New-Utf8Writer $p
  $header = @"
CHATGPT REPO BUNDLE (part $partNumber)
Generated: $(Get-Date -Format o)
RepoPath: $RepoPath
Rules:
- Each file is delimited by BEGIN/END markers and includes the relative path.
- Ask ChatGPT to search these bundles for symbols, strings, files, call chains, etc.
- If you upload multiple parts, tell ChatGPT: "You have BUNDLE_part001..N".

"@
  $w.WriteLine($header)
  return $w
}

$writer = Open-NewBundlePart -partNumber $part

# Manifest entries
$man = New-Object System.Collections.Generic.List[object]

foreach ($f in $selected) {
  $bytes = Read-FileBytes $f.FullPath
  if (Bytes-LookLikeBinary $bytes) { continue }

  $hash = (Get-FileHash -Path $f.FullPath -Algorithm SHA256).Hash.ToLowerInvariant()
  $text = Decode-BytesToText $bytes
  $text = Redact-Text $text

  $begin = "===== BEGIN FILE: {0} | bytes={1} | sha256={2} =====" -f $f.RelPath, $f.Bytes, $hash
  $end   = "===== END FILE: {0} =====" -f $f.RelPath

  $block = $begin + "`r`n" + $text + "`r`n" + $end + "`r`n`r`n"
  $blockBytes = [System.Text.Encoding]::UTF8.GetByteCount($block)

  if (($currentBytes + $blockBytes) -gt $chunkBytes -and $currentBytes -gt 0) {
    $writer.Flush()
    $writer.Dispose()

    $part++
    $currentBytes = 0
    $writer = Open-NewBundlePart -partNumber $part
  }

  $writer.Write($block)
  $currentBytes += $blockBytes

  $man.Add([pscustomobject]@{
    path   = $f.RelPath
    bytes  = $f.Bytes
    sha256 = $hash
    modified_utc = $f.Modified.ToString("o")
  }) | Out-Null
}

$writer.Flush()
$writer.Dispose()

# Write manifest JSON
$man | ConvertTo-Json -Depth 5 | Set-Content -Path $manifest -Encoding utf8

# ChatGPT prompt guide
@"
How to use these exports with ChatGPT (no GitHub connector):

1) Review the output folder: $OutDir
   - REPO_FILE_LIST.txt / .csv
   - MANIFEST.json
   - BUNDLE_part001.txt ... BUNDLE_partXXX.txt

2) Upload ALL BUNDLE_part*.txt files to ChatGPT.

3) In your message, say something like:
   "These are my repo bundles. Please search for '<symbol>' and show me where it's defined and called."

Helpful queries:
- "Find the definition of <Class/Function> and list call sites."
- "Trace the code path from <entrypoint> to <feature>."
- "Search for the string '<error message>' and propose a fix."
- "Summarize the architecture based on folder structure and key modules."

Privacy reminders:
- Uploading files shares their content with ChatGPT.
- Keep TrackedOnly on to avoid exporting untracked secrets.
- Consider enabling -RedactSecrets, but still review outputs before uploading.

"@ | Set-Content -Path $promptGuide -Encoding utf8

# Optional ZIP
if ($MakeZip) {
  $zipPath = Join-Path $OutDir "chatgpt_export.zip"
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zipPath
  Write-Host "Created ZIP: $zipPath"
}

Write-Host ""
Write-Host "Done."
Write-Host "Bundle parts:"
$bundlePaths | ForEach-Object { Write-Host (" - " + $_) }
Write-Host ""
Write-Host "Next: review the outputs, then upload BUNDLE_part*.txt to ChatGPT."
