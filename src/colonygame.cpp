// colonygame.cpp
// Windows-only, single-file colony game stub compatible with our Windows launcher.
// - No external dependencies (pure Win32 + GDI, Common Controls).
// - Accepts the same CLI/config flags used by the launcher: 
//   --config, --profile, --lang, --res WxH, --width, --height, --fullscreen, --vsync, --seed <n|random>, 
//   --safe-mode, --skip-intro, --validate
// - Writes/reads %APPDATA%\MarsColonySim\settings.ini, logs to %LOCALAPPDATA%\MarsColonySim\Logs
// - Returns 0 on --validate success, non-zero on failure.
//
// Build (MSVC dev prompt):
//   cl /EHsc /permissive- /W4 /DUNICODE /DWIN32_LEAN_AND_MEAN /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 colonygame.cpp ^
//      /link user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib comctl32.lib advapi32.lib ole32.lib
//
// Or use the CMakeLists.txt included at the end of this message.
//
// --------------------------------------------------------------------------------

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <objbase.h>
#include <Xinput.h> // XInput types/constants (we dynamically load the DLLs; no link lib required)
#include <shellscalingapi.h> // AdjustWindowRectExForDpi, GetDpiForSystem (optional)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cassert>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <random>
#include <deque>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <cmath>    // for std::cos, std::copysign, std::pow

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Prefer the high‑performance GPU on hybrid systems (NVIDIA/AMD).
// When building the full project with Option B, these are exported exactly once
// from src/platform/win/GpuPreference.cpp (added ONLY to the final EXE).
// For the single‑file build of this stub, keep a fallback export here that is
// compiled ONLY if COLONY_HAS_GPU_PREFERENCE_TU is NOT defined.
//
// Docs:
//  - NVIDIA Optimus (NvOptimusEnablement, DWORD; Release 302+):
//      https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
//  - AMD Hybrid Graphics (AmdPowerXpressRequestHighPerformance, DWORD):
//      https://gpuopen.com/learn/amdpowerxpressrequesthighperformance/
#if !defined(COLONY_HAS_GPU_PREFERENCE_TU)
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;          // Prefer dGPU on Optimus
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001; // Prefer dGPU on PowerXpress/Enduro
}
#endif

// Enable v6 Common Controls visual styles without a .manifest
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


//
// NOTE: For maintainability, the original single-file implementation has been split into
// smaller include units under src/colonygame/. This remains a *single translation unit*
// when compiled, preserving the original build workflow and behaviour.
//
#include "colonygame/colonygame_util.inc"
#include "colonygame/colonygame_logging.inc"
#include "colonygame/colonygame_config.inc"
#include "colonygame/colonygame_platform_win.inc"
#include "colonygame/colonygame_cli.inc"
#include "colonygame/colonygame_time_rng.inc"
#include "colonygame/colonygame_world.inc"
#include "colonygame/colonygame_render_gdi.inc"
#include "colonygame/colonygame_app_win32.inc"
#include "colonygame/colonygame_main.inc"
