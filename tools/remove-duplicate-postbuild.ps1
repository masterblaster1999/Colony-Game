# tools/remove-duplicate-postbuild.ps1
$ErrorActionPreference = 'Stop'
$path = Join-Path -LiteralPath (Get-Location) 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $path)) {
  throw "CMakeLists.txt not found at repo root: $path"
}

# Read as raw to keep original EOLs stable when we write back with UTF-8 (no BOM)
$text = [System.IO.File]::ReadAllText($path)
$lines = $text -split "(\r?\n)"
# $lines now holds tokens alternating: line, newline, line, newline...

$out = New-Object System.Collections.Generic.List[string]
$inBlock = $false
$parenDepth = 0

function Count-Parens([string]$s) {
  $open  = ([regex]::Matches($s, '\(')).Count
  $close = ([regex]::Matches($s, '\)')).Count
  return $open - $close
}

for ($i = 0; $i -lt $lines.Count; $i += 2) {
  $line = $lines[$i]
  $eol  = if ($i + 1 -lt $lines.Count) { $lines[$i + 1] } else { "" }

  if (-not $inBlock -and $line -match '^\s*add_custom_command\s*\(\s*TARGET\s+ColonyGame\s+POST_BUILD\b') {
    # Enter removal mode and initialize parentheses depth using the current line
    $inBlock   = $true
    $parenDepth = [Math]::Max(1, (Count-Parens $line))
    # Skip writing this line (we're deleting the whole call)
    continue
  }

  if ($inBlock) {
    $parenDepth += (Count-Parens $line)
    if ($parenDepth -le 0) {
      # We just closed the add_custom_command() call; stop removing
      $inBlock = $false
    }
    # Either way, skip writing lines while inBlock
    continue
  }

  # Keep lines that are not part of the removed block
  $out.Add($line)
  $out.Add($eol)
}

# Make a backup before overwriting
$backup = "$path.bak"
Copy-Item -LiteralPath $path -Destination $backup -Force

# Write back with UTF-8 (no BOM) and preserve newlines we collected
[System.IO.File]::WriteAllText($path, ($out -join ''), (New-Object System.Text.UTF8Encoding($false)))

Write-Host "✓ Removed duplicate top-level POST_BUILD custom command targeting 'ColonyGame' from $path"
Write-Host "  A backup was saved to: $backup"
