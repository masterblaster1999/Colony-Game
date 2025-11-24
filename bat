@echo off
setlocal

if not exist vcpkg\vcpkg.exe (
  echo Bootstrapping vcpkg...
  call vcpkg\bootstrap-vcpkg.bat -disableMetrics || goto :fail
)

echo Configuring build...
cmake -S . -B build -G "Visual Studio 17 2022" ^
  -DCMAKE_TOOLCHAIN_FILE=%CD%\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows || goto :fail

echo.
echo Done. Open build\Colony-Game.sln in Visual Studio.
goto :eof

:fail
echo.
echo *** Setup failed. Check messages above. ***
exit /b 1

