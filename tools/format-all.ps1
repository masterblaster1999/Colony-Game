$clang = "C:\Program Files\LLVM\bin\clang-format.exe"
Get-ChildItem "$PSScriptRoot\..\src" -Include *.h,*.hpp,*.cpp -Recurse |
  % { & $clang -i $_.FullName }
