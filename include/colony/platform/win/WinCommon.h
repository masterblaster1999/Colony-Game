// include/colony/platform/win/WinCommon.h
#pragma once

// Define lean header set & avoid std::min/max collisions only if not already set.
// (Prevents C4005 "macro redefinition" if also passed via /D in CMake/IDE.)
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

// Build Unicode Win32 APIs by default (common for modern apps)
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

// Target Windows 10+ APIs by default (0x0A00) – adjust if you need older support.
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif

#include <Windows.h>
