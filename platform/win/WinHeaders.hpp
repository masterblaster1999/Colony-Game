#pragma once

// Keep these *before* any Windows headers.
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
  #define NOMINMAX
#endif

// If you want Unicode WinAPI by default, prefer defining UNICODE/_UNICODE in CMake,
// but this is the rule: define it *before* including windows.h. :contentReference[oaicite:2]{index=2}
// #ifndef UNICODE
//   #define UNICODE
// #endif
// #ifndef _UNICODE
//   #define _UNICODE
// #endif

#include <Windows.h>

// Optional-but-safe explicit includes for things you used in your log:
#include <profileapi.h>   // QueryPerformanceCounter / QueryPerformanceFrequency
