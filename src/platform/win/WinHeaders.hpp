// src/platform/win/WinHeaders.hpp
#pragma once

// Define/guard Windows macros in exactly one place.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#  define NOMINMAX 1
#endif

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

#include <windows.h>
