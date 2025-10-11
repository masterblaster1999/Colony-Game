// platform/win/Paths.cpp
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include "Paths.h"
#include <windows.h>
#include <shlobj_core.h>   // SHGetKnownFolderPath, KNOWNFOLDERID
#include <objbase.h>       // CoTaskMemFree
#include <stdexcept>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace fs = std::filesystem;

namespace {
    fs::path KnownFolderPath(REFKNOWNFOLDERID id, DWORD flags = KF_FLAG_CREATE)
    {
        PWSTR w = nullptr;
        // SHGetKnownFolderPath retrieves paths for Known Folders (Vista+).
        // https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
        if (S_OK != ::SHGetKnownFolderPath(id, flags, nullptr, &w))
            throw std::runtime_error("SHGetKnownFolderPath failed");
        fs::path p = w;
        ::CoTaskMemFree(w);
        return p;
    }

    fs::path EnsureDir(const fs::path& p)
    {
        std::error_code ec;
        fs::create_directories(p, ec);
        return p;
    }
}

namespace Colony::Win::Paths
{
    fs::path SavedGames(std::wstring_view appName)
    {
        // FOLDERID_SavedGames is the canonical place for game saves on Windows.
        // KNOWNFOLDERID list: https://learn.microsoft.com/windows/win32/shell/knownfolderid
        auto base = KnownFolderPath(FOLDERID_SavedGames);
        return EnsureDir(base / appName);
    }

    fs::path LocalAppData(std::wstring_view appName)
    {
        auto base = KnownFolderPath(FOLDERID_LocalAppData);
        return EnsureDir(base / appName);
    }

    fs::path RoamingAppData(std::wstring_view appName)
    {
        auto base = KnownFolderPath(FOLDERID_RoamingAppData);
        return EnsureDir(base / appName);
    }

    fs::path Logs(std::wstring_view appName)
    {
        return EnsureDir(LocalAppData(appName) / L"logs");
    }

    fs::path Config(std::wstring_view appName)
    {
        return EnsureDir(RoamingAppData(appName) / L"config");
    }
}
