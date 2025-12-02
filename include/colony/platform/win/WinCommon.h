#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00 // Target Windows 10+
#endif

#include <Windows.h>
