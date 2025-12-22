// engine/win/DpiAware.cpp
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>

#include "DpiAware.h"

namespace winplat
{
bool EnablePerMonitorV2DpiAwareness()
{
    // Prefer a manifest, but this is a safe runtime opt-in if called early.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32)
        return false;

    using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    auto fn = reinterpret_cast<Fn>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (!fn)
        return false;

    return fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}
} // namespace winplat
