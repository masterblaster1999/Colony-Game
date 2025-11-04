// Use the project-wide Windows header policy (defines WIN32_LEAN_AND_MEAN, NOMINMAX, STRICT
// and includes <Windows.h> once). Avoids C4005 macro redefinitions.
#include "platform/win/WinCommon.h"

#include <filesystem>
#include <string>
#include <vector>

namespace win {

// Robustly get the full path to the current module (the EXE) without relying on MAX_PATH.
// If the buffer is truncated, GetModuleFileNameW returns the buffer size; we grow and retry.
static std::wstring GetModuleFileNameW_Dynamic(HMODULE module = nullptr)
{
    // Start with a reasonable capacity and grow as needed.
    DWORD cap = 260; // initial guess; will grow as required
    // Put a hard ceiling to avoid runaway growth if something is badly wrong.
    const DWORD kMaxCap = 32768; // matches Win32 extended-length path max (UTF-16 characters)

    for (;;)
    {
        std::vector<wchar_t> buf(cap);
        DWORD n = ::GetModuleFileNameW(module, buf.data(), cap);

        if (n == 0) {
            // Failure (should be rare for the current module). Return empty.
            return std::wstring();
        }

        // If truncated, GetModuleFileName(W) returns n == cap (or effectively fills the buffer).
        // Grow and retry until the returned length clearly fits.
        if (n >= cap - 1) {
            if (cap >= kMaxCap) {
                // Give up rather than looping forever; return what we have.
                return std::wstring(buf.data(), buf.data() + (std::min<DWORD>(n, cap - 1)));
            }
            cap = std::min<DWORD>(cap * 2, kMaxCap);
            continue;
        }

        // Success: n is the number of characters not including the trailing null.
        return std::wstring(buf.data(), buf.data() + n);
    }
}

// Optional convenience if callers ever need the full executable path.
static std::filesystem::path ExecutablePath()
{
    std::wstring w = GetModuleFileNameW_Dynamic(nullptr);
    if (w.empty()) {
        // Fallback: try the current directory (best effort; may throw if not accessible)
        try { return std::filesystem::current_path(); }
        catch (...) { return {}; }
    }
    return std::filesystem::path(std::move(w));
}

std::filesystem::path ExecutableDir()
{
    auto exe = ExecutablePath();
    if (exe.empty()) {
        try { return std::filesystem::current_path(); }
        catch (...) { return {}; }
    }
    return exe.parent_path();
}

void SetWorkingDirToExecutableDir()
{
    try {
        std::filesystem::current_path(ExecutableDir());
    } catch (...) {
        // Leave CWD unchanged on failure.
    }
}

} // namespace win
