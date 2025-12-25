param()

$ErrorActionPreference = "Stop"

function Find-ClangFormat {
    $candidates = @(
        "$env:LLVM_INSTALL_DIR\bin\clang-format.exe",
        "C:\Program Files\LLVM\bin\clang-format.exe",
        "C:\Program Files (x86)\LLVM\bin\clang-format.exe"
    )

    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }

    $cmd = Get-Command clang-format.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    throw "clang-format.exe not found. Install LLVM (clang-format) or add it to PATH."
}

$clang = Find-ClangFormat
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

# Only format first-party code. Avoid reformatting vendored code under external/ etc.
$targets = @(
    Join-Path $repoRoot "src",
    Join-Path $repoRoot "include",
    Join-Path $repoRoot "tests"
) | Where-Object { $_ -and (Test-Path $_) }

if ($targets.Count -eq 0) {
    throw "No source directories found to format."
}

Get-ChildItem -Path $targets -Include *.h,*.hpp,*.cpp -Recurse -File |
    ForEach-Object { & $clang -i $_.FullName }
