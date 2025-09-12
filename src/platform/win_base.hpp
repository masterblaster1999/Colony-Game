// src/platform/win_base.hpp
#pragma once

// Define Windows macros exactly once, project-wide
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

// Tiny helper for unused variables/parameters
#define CG_UNUSED(x) (void)(x)
