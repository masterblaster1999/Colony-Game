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
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH)
    {
        // Fall back to current working directory if we can't query the exe path.
        return fs::current_path();
    }

    // Use the helper so all wide->path conversions are centralized.
    const std::wstring exePath(buf, n);
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
