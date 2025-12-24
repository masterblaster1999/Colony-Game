#include "WinFiles.h"
#include <windows.h>
#include <shlobj_core.h>  // SHGetKnownFolderPath
#include <knownfolders.h> // FOLDERID_LocalAppData
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace platform::win {

[[nodiscard]]
static fs::path WStringToPath(const std::wstring& w)
{
    return fs::path(w);
}

[[nodiscard]]
fs::path GetExeDir()
{
    // Long-path safe GetModuleFileNameW: grow buffer until it fits.
    // (On truncation, GetModuleFileNameW returns the buffer size.)
    std::wstring buf(260, L'\0');
    for (;;)
    {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
        {
            // Fall back to current working directory if we can't query the exe path.
            return fs::current_path();
        }

        if (n < buf.size())
        {
            buf.resize(n);
            break;
        }

        // Defensive upper bound: Win32 paths are limited to 32k-ish wide chars.
        // If we ever hit it, just use the truncated prefix we have.
        if (buf.size() >= 32768)
        {
            buf.resize(n);
            break;
        }

        buf.resize(buf.size() * 2);
    }

    // Use the helper so all wide->path conversions are centralized.
    const std::wstring exePath = buf;
    fs::path p = WStringToPath(exePath);
    return p.parent_path();
}

void FixWorkingDirectory()
{
    const fs::path dir = GetExeDir();
    // If GetExeDir failed and returned an empty path, don't change CWD.
    if (!dir.empty())
    {
        SetCurrentDirectoryW(dir.wstring().c_str());
    }
}

[[nodiscard]]
static fs::path KnownFolderPath(REFKNOWNFOLDERID id)
{
    PWSTR w = nullptr;
    fs::path out;

    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w)))
    {
        // Convert PWSTR to std::wstring, then to fs::path via our helper.
        out = WStringToPath(std::wstring(w));
    }

    if (w)
    {
        CoTaskMemFree(w);
    }

    return out;
}

[[nodiscard]]
fs::path GetSaveDir()
{
    // Prefer LocalAppData\ColonyGame; if that fails, fall back to exe directory.
    fs::path base = KnownFolderPath(FOLDERID_LocalAppData);
    if (base.empty())
    {
        base = GetExeDir();
    }

    fs::path p = base / L"ColonyGame";

    std::error_code ec;
    fs::create_directories(p, ec); // ignore error, just return the path
    return p;
}

[[nodiscard]]
fs::path GetLogDir()
{
    fs::path p = GetSaveDir() / L"logs";

    std::error_code ec;
    fs::create_directories(p, ec); // ignore error, just return the path
    return p;
}

} // namespace platform::win
