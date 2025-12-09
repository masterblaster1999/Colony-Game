// src/app/CrashHandler.h
#pragma once

#ifdef _WIN32
    // Make this path truly relative to *this* header so it works regardless of include dirs
    #include "../../platform/win/CrashHandlerWin.h"

    inline void InstallCrashHandler(const wchar_t* appName)
    {
        // Allow caller to pass a title; fallback is stable.
        wincrash::InitCrashHandler(appName ? appName : L"Colony Game");
    }
#else
    inline void InstallCrashHandler(const wchar_t*) {}
#endif
