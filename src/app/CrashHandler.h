#pragma once

#ifdef _WIN32
    #include "platform/win/CrashHandlerWin.h"

    inline void InstallCrashHandler(const wchar_t* appName)
    {
        // You can pass the name through if you want it configurable.
        wincrash::InitCrashHandler(appName ? appName : L"Colony Game");
    }
#else
    inline void InstallCrashHandler(const wchar_t*) {}
#endif
