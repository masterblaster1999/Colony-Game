// src/platform/win/WinHeaders.h
#pragma once

// Define only if not already defined by the compiler command line.
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
// add others you actually use (dxgi.h, d3d11.h, xinput.h, etc.)
