#include "platform/win/WinPaths.h"
#include <windows.h>
#include <ShlObj.h>     // SHGetKnownFolderPath
#include <cstdlib>      // _wgetenv

namespace cg::winpaths {
    static std::filesystem::path from_known_folder(REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) {
            std::filesystem::path out(p);
            CoTaskMemFree(p);
            return out;
        }
        return {};
    }

    std::filesystem::path local_appdata() {
        if (auto p = from_known_folder(FOLDERID_LocalAppData); !p.empty())
            return p;
        if (const wchar_t* env = _wgetenv(L"LOCALAPPDATA"))
            return std::filesystem::path(env);
        if (const wchar_t* home = _wgetenv(L"USERPROFILE"))
            return std::filesystem::path(home) / L"AppData" / L"Local";
        return std::filesystem::current_path(); // last‑ditch fallback
    }

    std::filesystem::path save_root() {
        auto p = local_appdata() / L"ColonyGame";
        std::error_code ec; std::filesystem::create_directories(p, ec);
        return p;
    }

    std::filesystem::path ensure_profile_dir(const std::string& profile) {
        auto p = save_root() / L"Saves" / std::filesystem::path(profile);
        std::error_code ec; std::filesystem::create_directories(p, ec);
        return p;
    }
}
