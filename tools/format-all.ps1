# tools/format-all.ps1
#
# Formats C/C++ sources using clang-format.
#
# Usage:
#   pwsh -File tools/format-all.ps1
#   pwsh -File tools/format-all.ps1 -ClangFormat "C:\Program Files\LLVM\bin\clang-format.exe"
#
# Notes:
#   - Uses .clang-format at the repo root (clang-format automatically discovers it).
#   - Windows-friendly: attempts to locate clang-format via PATH or common LLVM installs.

param(
    [string]$ClangFormat = ""
)

function Find-ClangFormat {
    param([string]$Explicit)

    if ($Explicit -and (Test-Path $Explicit)) {
        return (Resolve-Path $Explicit).Path
    }

    $cmd = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Path
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles       "LLVM\bin\clang-format.exe"),
        (Join-Path $env:ProgramFiles(x86)  "LLVM\bin\clang-format.exe")
    )

    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) {
            return $c
        }
    }

    return $null
}

$clang = Find-ClangFormat -Explicit $ClangFormat
if (-not $clang) {
    Write-Error "clang-format not found. Install LLVM (clang-format) or pass -ClangFormat <path>."
    exit 1
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$roots = @(
    (Join-Path $repoRoot "src"),
    (Join-Path $repoRoot "include"),
    (Join-Path $repoRoot "tests")
) | Where-Object { Test-Path $_ }

Get-ChildItem -Path $roots -Include *.h,*.hpp,*.cpp,*.inl -Recurse |
    ForEach-Object {
        & $clang -i $_.FullName
    }

Write-Host ("Formatted sources using: " + $clang)
