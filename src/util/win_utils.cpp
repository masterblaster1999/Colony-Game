// src/util/win_utils.cpp

#include "util/win_utils.h"

#ifndef _WIN32
#  error "win_utils.cpp is Windows-only and should not be built on non-Windows platforms."
#endif

// Prefer the project's unified Windows header if available; otherwise fall
// back to the standard Windows.h + trim common macro noise.
#ifdef __has_include
#  if __has_include("platform/win/WinHeaders.h")
#    include "platform/win/WinHeaders.h"
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <Windows.h>
#  endif
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

// ShellExecuteW / ShellExecuteExW live here (especially with WIN32_LEAN_AND_MEAN)
#include <shellapi.h>

namespace
{
    // Per ShellExecuteW docs: returns HINSTANCE that can be cast to INT_PTR.
    // > 32  == success
    // <= 32 == error code (SE_ERR_XXX or similar). :contentReference[oaicite:2]{index=2}
    inline void ShellExecuteSafe(
        LPCWSTR verb,
        LPCWSTR file,
        LPCWSTR parameters,
        LPCWSTR directory,
        int     showCmd)
    {
        HINSTANCE h = ::ShellExecuteW(
            nullptr,   // parent window
            verb,
            file,
            parameters,
            directory,
            showCmd);

        const auto code = static_cast<INT_PTR>(h);
        if (code <= 32)
        {
            // For now we just emit a debug string; you can wire this into your
            // logging system if desired and map 'code' to SE_ERR_* / ERROR_*.
            ::OutputDebugStringW(L"[util::ShellExecuteSafe] ShellExecuteW failed.\n");
        }
    }
} // anonymous namespace

namespace util
{
    std::string Quoted(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        out.append(s);
        out.push_back('"');
        return out;
    }

    void OpenInExplorer(const std::filesystem::path& path)
    {
        const std::wstring wpath = path.wstring();

        // Check attributes to see if this is a directory
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        const bool isDir =
            attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDir)
        {
            // Just open the directory
            ShellExecuteSafe(L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }

        // Otherwise, ask Explorer to select the file
        std::wstring args = L"/select,";
        args += wpath;

        ShellExecuteSafe(L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
} // namespace util
