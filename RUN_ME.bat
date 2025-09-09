@echo off
setlocal EnableDelayedExpansion
title Colony-Game — one-click build & run

rem --- paths ---
set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "LOG=%BUILD%\oneclick.log"
if not exist "%BUILD%" mkdir "%BUILD%"

echo [%DATE% %TIME%] Starting build >"%LOG%"

rem --- prefer CMake (MSVC if available, else MinGW) ---
where cmake >nul 2>&1
if %errorlevel%==0 goto USE_CMAKE

:TRY_GPP
where g++ >nul 2>&1
if not %errorlevel%==0 goto NO_TOOLS

echo Using g++ fallback... >>"%LOG%"
set "SOURCES="
for %%F in ("%ROOT%src\*.cpp") do (
  if /I not "%%~nxF"=="SingleClick.cpp" if /I not "%%~nxF"=="WinLauncher.cpp" (
    set "SOURCES=!SOURCES! "%%F""
  )
)
pushd "%ROOT%"
g++ -std=c++20 -O2 -DNOMINMAX -o "%BUILD%\ColonyGame.exe" %SOURCES% >>"%LOG%" 2>&1
if errorlevel 1 goto BUILD_FAIL
start "" "%BUILD%\ColonyGame.exe"
popd
exit /b 0

:USE_CMAKE
rem Try MSVC toolchain if present
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
  if defined VSINSTALL call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)

cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release >>"%LOG%" 2>&1
if errorlevel 1 goto CMAKE_TO_GPP
cmake --build "%BUILD%" --config Release --parallel >>"%LOG%" 2>&1
if errorlevel 1 goto CMAKE_TO_GPP

set "EXE=%BUILD%\ColonyGame.exe"
if exist "%BUILD%\Release\ColonyGame.exe" set "EXE=%BUILD%\Release\ColonyGame.exe"
if not exist "%EXE%" goto BUILD_FAIL
start "" "%EXE%"
exit /b 0

:CMAKE_TO_GPP
echo CMake/MSVC path failed; trying MinGW g++... >>"%LOG%"
goto TRY_GPP

:NO_TOOLS
echo.
echo ❌ No compiler found.
echo    Install either:
echo      - Visual Studio Build Tools (C++), or
echo      - MinGW-w64 (g++) and CMake
echo.
echo See oneclick.log for details.
exit /b 1

:BUILD_FAIL
echo.
echo ❌ Build failed. See "%LOG%" for errors.
exit /b 1
