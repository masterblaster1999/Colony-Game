$dumpRoot = Join-Path $env:LOCALAPPDATA "ColonyGame\crashdumps"
$dump = Get-ChildItem -Path $dumpRoot -Filter *.dmp | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dump) { throw "No .dmp files in $dumpRoot" }

$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
if (!(Test-Path $cdb)) { throw "Install Debugging Tools for Windows." }

$out = [IO.Path]::ChangeExtension($dump.FullName, ".analysis.txt")
& $cdb -z $dump.FullName -c "!analyze -v; .ecxr; k; lm; q" | Out-File -Encoding utf8 $out
Write-Host "Wrote $out"
