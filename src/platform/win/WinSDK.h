// src/platform/win/WinSDK.h
#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shlwapi.h>  // if you use Path* helpers
#pragma comment(lib, "Shlwapi.lib")
