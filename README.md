# Colony Game (Windows-only)

A personal learning project: a colony-sim prototype built in modern C++ (C++23) with a Windows/DirectX/SDL2 toolchain.

## Status

This repository is under active iteration and contains a mix of:
- the main game executable (`ColonyGame`)
- experimental tools / demos
- a modular “engine/core/platform” split in progress

Expect rough edges and refactors.

## Build (Windows)

### Prerequisites
- Windows 10/11
- Visual Studio 2022 (or Build Tools) with **Desktop development with C++**
- CMake 3.27+
- Ninja (recommended)
- vcpkg (manifest mode; repo contains `vcpkg.json`)

### Quick start (recommended)
From a **Developer PowerShell** (or regular PowerShell if VS is installed and on PATH):

```powershell
# Bootstraps VS dev shell, resolves vcpkg, installs manifest deps, and configures a build folder
pwsh -File tools/setup-dev.ps1 -InstallMissingTools -InstallVSBuildTools -InstallVcpkg -ManifestInstall -Configure -BuildDir out/build/windows-msvc-x64

# Build
cmake --build out/build/windows-msvc-x64 --config Debug
```

If you prefer CMake presets in Visual Studio / VS Code, see `CMakePresets.json`.

## Runtime controls (current prototype)

In the current window prototype (`AppWindow`):

- **Esc**: quit
- **V**: toggle VSync (when off, frame rate is capped to avoid 100% CPU)
- **F11** or **Alt+Enter**: toggle borderless fullscreen
- Mouse drag (LMB/RMB/MMB): camera placeholder controls (debug title shows values)

### Per-user settings

The prototype persists a small settings file at:

- `%LOCALAPPDATA%\ColonyGame\settings.json`

New in this patch:

- `runtime.pauseWhenUnfocused` (default `true`): when you Alt+Tab away, the game pauses rendering/sim to save CPU/GPU.
- `runtime.maxFpsWhenUnfocused` (default `30`): if `pauseWhenUnfocused` is `false`, this caps the background FPS.

Example snippet:

```json
{
  "runtime": {
    "pauseWhenUnfocused": false,
    "maxFpsWhenUnfocused": 30
  }
}
```

## Notes
- This project intentionally **does not support Linux/macOS** at the moment.
- If you hit CMake errors about missing helper commands, ensure you are configuring from the repository root (not a subfolder).
