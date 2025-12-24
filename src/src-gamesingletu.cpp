// ============================================================================
// src-gamesingletu.cpp
// Single translation unit gameplay module for Colony-Game (Windows-only).
//
// Public surface (callable from Launcher.cpp without a header):
//   struct GameOptions { int width, height; bool fullscreen, vsync, safeMode;
//                        uint64_t seed; std::string profile, lang, saveDir, assetsDir; };
//   int RunColonyGame(const GameOptions&);
//
// This TU owns: window loop, input, world gen, A* pathfinding, colonists & jobs,
// buildings/economy, HUD, and save/load — all using Win32 + GDI (no external deps).
//
// Integration steps:
//   1) Add this file to the build.
//   2) In Launcher.cpp (after you computed effective settings/paths), build a GameOptions
//      instance and call RunColonyGame(go).
//   3) (Optional) Keep --validate in your launcher to check assets folder; this TU
//      does not implement validation because it is called post-bootstrap.
//
// Platform:
//   Windows 10+ (uses DPI awareness and Common Controls). Pure Win32; no CONSOLE.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <objbase.h>

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
#include <cmath>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Enable v6 Common Controls visual styles without a .manifest
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


// NOTE: This file has been split into smaller *.inl parts under src/game/singletu/
//       to improve readability while still compiling as a single translation unit.

#include "game/singletu/STU_PublicInterface.inl"
#include "game/singletu/STU_Utilities.inl"
#include "game/singletu/STU_Logging.inl"
#include "game/singletu/STU_MathTypes.inl"
#include "game/singletu/STU_RNG.inl"
#include "game/singletu/STU_WorldTiles.inl"
#include "game/singletu/STU_Pathfinding.inl"
#include "game/singletu/STU_EconomyEntities.inl"
#include "game/singletu/STU_RenderingGDI.inl"
#include "game/singletu/STU_GameImpl.inl"
#include "game/singletu/STU_EntryPoint.inl"
