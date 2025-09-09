param(
  [switch]$apply  # run with -apply to write changes; otherwise it's a dry-run
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $root "..")

$files = Get-ChildItem -Path $repo -Recurse -Include *.cpp,*.hpp,*.h | Where-Object { -not $_.FullName.Contains("\build\") }

$regexes = @(
  @{ name = "numeric_limits::max()" ; pattern = 'std::numeric_limits<([^>]+)>::max\(\)' ; replace = '(std::numeric_limits<$1>::max)()' },
  @{ name = "numeric_limits::min()" ; pattern = 'std::numeric_limits<([^>]+)>::min\(\)' ; replace = '(std::numeric_limits<$1>::min)()' },
  @{ name = "wcout.write narrow literal" ; pattern = 'std::wcout\.write\("([^"]*)"' ; replace = 'std::wcout.write(L"$1"' }
)

$issues_found = @()

foreach ($f in $files) {
  $text = Get-Content -Raw -LiteralPath $f.FullName
  $orig = $text
  foreach ($rx in $regexes) {
    $text = [regex]::Replace($text, $rx.pattern, $rx.replace)
  }

  # report likely optional/seed problems (no auto-fix)
  $m1 = [regex]::Matches($text, 'value_or\(([^)]+)\)')
  foreach ($m in $m1) {
    $arg = $m.Groups[1].Value
    if ($arg -notmatch '^\s*\d+[uU]?[lL]*\s*$' -and $arg -notmatch '^\s*[A-Za-z_][A-Za-z0-9_]*\s*\(') {
      $issues_found += "⚠ value_or() candidate in $($f.Name): $($m.Value)"
    }
  }
  $m2 = [regex]::Matches($text, '(?m)^\s*Rng\s+rng\s*;')
  foreach ($m in $m2) {
    $issues_found += "⚠ default-constructed Rng in $($f.Name) at: $($m.Value.Trim())"
  }

  if ($apply -and $text -ne $orig) {
    Set-Content -NoNewline -LiteralPath $f.FullName -Value $text
    Write-Host "Edited: $($f.FullName)"
  } elseif ($text -ne $orig) {
    Write-Host "[DRY-RUN] would edit: $($f.FullName)"
  }
}

if ($issues_found.Count -gt 0) {
  "`n--- Potential manual fixes ---"
  $issues_found | Sort-Object -Unique | ForEach-Object { $_ }
  "`nSuggestions:"
  " - Replace value_or(<something>) with a real uint64_t default, e.g. value_or(12345ULL)"
  " - If 'Rng rng;' fails, pass a seed: 'Rng rng{seed};'"
}
