#pragma once
#include <Windows.h>

// Small helper to invoke a function under SEH in a way that avoids C++ unwinding.
//
// Use when you want to isolate code that might raise an SEH exception and handle
// it via a custom filter (e.g., write a crash dump, then continue/abort).
using VoidFunc = void(__cdecl*)();

inline void SehInvokeNoUnwind(VoidFunc fn,
                              LONG(__stdcall* filter)(EXCEPTION_POINTERS*)) noexcept
{
    __try
    {
        fn();
    }
    __except (filter(GetExceptionInformation()))
    {
        // Swallow: the filter is expected to log and/or write a dump.
    }
}
