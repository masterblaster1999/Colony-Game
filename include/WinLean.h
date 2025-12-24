// include/WinLean.h
#pragma once

// Lightweight Windows include.
//
// Prefer including your project's centralized Windows header policy
// (e.g., platform/win/WinCommon.h) when possible so WIN32_LEAN_AND_MEAN /
// NOMINMAX / STRICT are consistent across translation units.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif

#include <Windows.h>
