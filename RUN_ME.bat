@echo off
setlocal EnableDelayedExpansion
title Colony-Game — one-click build & run

rem --- dirs & log ---
set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "LOG=%BUILD%\oneclick.log"
if not exist "%BUILD%" mkdir "%BUILD%"
echo [%DATE% %TIME%] starting build > "%LOG%"

rem --- prefer CMake ---
where cmake >nul 2>&1 || (echo ❌ CMake not found. Install CMake and retry.& exit /b 1)

rem --- try to enable MSVC environment if present ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
  if defined VSINSTALL call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >>"%LOG%" 2>&1
)

rem --- detect vcpkg (optional) for SDL2 ---
set "VCPKG_ROOT_HINT=%VCPKG_ROOT%"
if not defined VCPKG_ROOT_HINT if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT_HINT=C:\vcpkg"
if defined VCPKG_ROOT_HINT (
  set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT_HINT:\=\\%\scripts\buildsystems\vcpkg.cmake"
  echo using vcpkg at %VCPKG_ROOT_HINT% >>"%LOG%"
  rem Pre-install SDL2 on Windows triplet (won't fail build if it errors)
  if exist "%VCPKG_ROOT_HINT%\vcpkg.exe" (
    "%VCPKG_ROOT_HINT%\vcpkg.exe" install sdl2:x64-windows >>"%LOG%" 2>&1
  )
)

rem --- configure ---
cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% >>"%LOG%" 2>&1
if errorlevel 1 goto FAIL

rem --- build ---
cmake --build "%BUILD%" --config Release --parallel >>"%LOG%" 2>&1
if errorlevel 1 goto FAIL

rem --- locate exe & run ---
set "EXE=%BUILD%\ColonyGame.exe"
if exist "%BUILD%\Release\ColonyGame.exe" set "EXE=%BUILD%\Release\ColonyGame.exe"
if not exist "%EXE%" goto FAIL
echo.
echo ✅ Build OK. Launching: %EXE%
start "" "%EXE%"
exit /b 0

:FAIL
echo.
echo ❌ Build failed. See "%LOG%" for details.
exit /b 1
