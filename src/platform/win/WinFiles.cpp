#include "WinFiles.h"
#include <windows.h>
#include <shlobj_core.h>  // SHGetKnownFolderPath
#include <knownfolders.h> // FOLDERID_LocalAppData
#include <filesystem>

namespace fs = std::filesystem;

namespace platform::win {

static fs::path WStringToPath(const std::wstring& w) { return fs::path(w); }

fs::path GetExeDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return fs::current_path();
    fs::path p(buf);
    return p.parent_path();
}

void FixWorkingDirectory() {
    const fs::path dir = GetExeDir();
    SetCurrentDirectoryW(dir.wstring().c_str());
}

static fs::path KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR w = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w))) {
        out = fs::path(w);
    }
    if (w) CoTaskMemFree(w);
    return out;
}

fs::path GetSaveDir() {
    fs::path p = KnownFolderPath(FOLDERID_LocalAppData) / L"ColonyGame";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

fs::path GetLogDir() {
    fs::path p = GetSaveDir() / L"logs";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

} // namespace platform::win
