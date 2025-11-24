// src/platform/win/WinSDK.h
//
// Central place for Windows SDK configuration & includes.
// Windows-only. Used by PathUtilWin.cpp and other platform/win files.

#pragma once

// If the project didn't already set a target Windows version,
// default to Windows 10 (0x0A00). If it's already defined in
// the build flags or a PCH, these guards keep us from changing it.
#ifndef WINVER
#   define WINVER 0x0A00
#endif

#ifndef _WIN32_WINNT
#   define _WIN32_WINNT WINVER
#endif

// Make the Windows headers a bit cleaner. If these are already defined
// on the command line, we don't redefine them.
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#   define NOMINMAX
#endif

// Core Windows SDK
#include <Windows.h>

// Common shell / COM headers that many platform/win files use.
// (If you don't need these in a particular TU, that's fine – they
// will simply be available.)
#include <ShlObj.h>
#include <Objbase.h>
