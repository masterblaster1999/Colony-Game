// src/platform/win/WinPath.cpp
// Windows-only helpers for locating user-facing folders used by Colony-Game.
// Provides: winpath::saved_games_dir()

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj_core.h>   // SHGetKnownFolderPath
#include <knownfolders.h>  // FOLDERID_*
#include <objbase.h>       // CoTaskMemFree
#include <filesystem>
#include <string>

namespace {

std::filesystem::path from_known_folder(REFKNOWNFOLDERID id) noexcept
{
    PWSTR wide = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &wide);
    if (SUCCEEDED(hr) && wide) {
        std::filesystem::path p = wide;
        CoTaskMemFree(wide);
        return p;
    }
    return {};
}

std::filesystem::path from_env(const wchar_t* var) noexcept
{
    wchar_t buf[32768];
    const DWORD n = GetEnvironmentVariableW(var, buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return std::filesystem::path(buf);
    return {};
}

} // anon

namespace winpath {

// Returns the user's "Saved Games" directory, with robust fallbacks.
//
// Order:
//   1) Known Folder FOLDERID_SavedGames  (C:\Users\<user>\Saved Games)
//   2) Known Folder FOLDERID_Documents / "My Games"  (older titles commonly used this)
//   3) %USERPROFILE% / "Saved Games"
//   4) Current working directory (last‑ditch)
std::filesystem::path saved_games_dir() noexcept
{
    // 1) Preferred: dedicated Saved Games known folder.
    if (auto p = from_known_folder(FOLDERID_SavedGames); !p.empty())
        return p;

    // 2) Historical fallback: Documents\My Games
    if (auto docs = from_known_folder(FOLDERID_Documents); !docs.empty())
        return docs / L"My Games";

    // 3) Environment fallback
    if (auto prof = from_env(L"USERPROFILE"); !prof.empty())
        return prof / L"Saved Games";

    // 4) Very last fallback
    return std::filesystem::current_path();
}

} // namespace winpath
