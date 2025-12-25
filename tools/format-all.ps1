# Format all C/C++ sources in src/ using clang-format.
#
# Prefers clang-format on PATH, but falls back to common Windows installs:
#  - LLVM official installer
#  - Visual Studio's bundled LLVM toolchain

$clang = $null

$cmd = Get-Command clang-format.exe -ErrorAction SilentlyContinue
if ($cmd) {
  $clang = $cmd.Source
}

if (-not $clang) {
  $candidatePaths = @(
    "C:\Program Files\LLVM\bin\clang-format.exe",
    "C:\Program Files (x86)\LLVM\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe"
  )

  foreach ($p in $candidatePaths) {
    if (Test-Path $p) {
      $clang = $p
      break
    }
  }
}

if (-not $clang) {
  Write-Error "clang-format.exe not found. Install LLVM or ensure clang-format is on PATH."
  exit 1
}

Write-Host "Using clang-format: $clang"

Get-ChildItem "$PSScriptRoot\..\src" -Include *.h,*.hpp,*.cpp -Recurse |
  % { & $clang -i $_.FullName }
