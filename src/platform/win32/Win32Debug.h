#pragma once

// Win32Debug.h
// ------------
// Small, dependency-free debug helpers for Windows.
//
// This header exists primarily to provide a stable include target for code that
// uses:  #include "platform/win32/Win32Debug.h"
//
// It is intentionally header-only (no .cpp required).

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace colony::appwin::win32 {

// Returns true if a debugger is attached to the process.
[[nodiscard]] inline bool IsDebuggerAttached() noexcept
{
    return ::IsDebuggerPresent() != FALSE;
}

// Triggers a breakpoint if a debugger is attached.
inline void DebugBreakIfAttached() noexcept
{
    if (IsDebuggerAttached())
        ::DebugBreak();
}

// Write a narrow string to the Visual Studio Output window (and any debugger).
inline void DebugTraceA(const char* s) noexcept
{
#if defined(_DEBUG)
    if (!s)
        return;

    ::OutputDebugStringA(s);
    const size_t n = std::strlen(s);
    if (n > 0 && s[n - 1] != '\n')
        ::OutputDebugStringA("\n");
#else
    (void)s;
#endif
}

// Write a wide string to the Visual Studio Output window (and any debugger).
inline void DebugTraceW(const wchar_t* s) noexcept
{
#if defined(_DEBUG)
    if (!s)
        return;

    ::OutputDebugStringW(s);
    const size_t n = std::wcslen(s);
    if (n > 0 && s[n - 1] != L'\n')
        ::OutputDebugStringW(L"\n");
#else
    (void)s;
#endif
}

// printf-style debug output (narrow).
inline void DebugPrintfA(const char* fmt, ...) noexcept
{
#if defined(_DEBUG)
    if (!fmt)
        return;

    char buf[2048]{};

    va_list ap;
    va_start(ap, fmt);
    (void)std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    DebugTraceA(buf);
#else
    (void)fmt;
#endif
}

// printf-style debug output (wide).
inline void DebugPrintfW(const wchar_t* fmt, ...) noexcept
{
#if defined(_DEBUG)
    if (!fmt)
        return;

    wchar_t buf[2048]{};

    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    DebugTraceW(buf);
#else
    (void)fmt;
#endif
}

// --------------------------------------------------------------------------------------
// Optional assert macro (kept local to this header; does not replace the project's asserts)
// --------------------------------------------------------------------------------------
#ifndef COLONY_WIN32_ASSERT
    #if defined(_DEBUG)
        #define COLONY_WIN32_ASSERT(expr)                                        \
            do {                                                                 \
                if (!(expr)) {                                                   \
                    ::OutputDebugStringA("[Colony] Assert failed: " #expr "\n"); \
                    ::DebugBreak();                                              \
                }                                                                \
            } while (0)
    #else
        #define COLONY_WIN32_ASSERT(expr) ((void)0)
    #endif
#endif

} // namespace colony::appwin::win32
