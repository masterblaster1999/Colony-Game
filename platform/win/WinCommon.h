// platform/win/WinCommon.h
#pragma once

// Define once, behind guards, and before <windows.h>.
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
  #define NOMINMAX 1           // avoid min/max macro collisions with <algorithm>
#endif
#ifndef STRICT
  #define STRICT 1             // stricter Win32 typedefs
#endif

#include <windows.h>
#include <windowsx.h>
