// src/platform/win/WinCommon.h
#pragma once

// Only define these if not already provided by the build (prevents C4005).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

// Strongly recommended for Windows C APIs:
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

// Target at least Windows 10 features at compile time (adjust if needed).
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00 // Windows 10
#endif

// Now pull in the Windows headers
#include <Windows.h>
