#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "LauncherConfig.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <cwchar>
#include <windows.h>

#include "platform/win/WinPaths.h"  // matches the actual file in src/platform/win/

namespace fs = std::filesystem;

namespace {

static inline void trim(std::wstring& s) {
    auto iswspace = [](wchar_t c){ return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'; };
    size_t b=0, e=s.size();
    while (b<e && iswspace(s[b])) ++b;
    while (e>b && iswspace(s[e-1])) --e;
    s = s.substr(b, e-b);
}

// Local Windows-only fallback for locating the resources directory.
// Walks up from the EXE folder looking for a "resources" directory.
// If not found, falls back to the EXE directory.
static fs::path resource_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path exeDir = (len ? fs::path(buf).parent_path() : fs::path(L"."));

    for (fs::path cur = exeDir; !cur.empty(); cur = cur.parent_path()) {
        const fs::path cand = cur / L"resources";
        std::error_code ec;
        if (fs::exists(cand, ec)) {
            return cand;
        }
        if (cur == cur.root_path()) break;
    }
    return exeDir; // fallback
}

} // namespace

namespace launcher {

std::wstring read_target_exe() {
    const fs::path cfg = resource_dir() / L"launcher.cfg";
    std::wifstream in(cfg);
    if (in) {
        std::wstring line;
        std::getline(in, line);
        // Strip UTF-16 BOM if present
        if (!line.empty() && (line[0] == 0xFEFF)) line.erase(0, 1);
        trim(line);
        if (!line.empty() && line[0] != L'#' && line.rfind(L"//", 0) == std::wstring::npos) {
            return line;
        }
    }
    return L"ColonyGame.exe";
}

} // namespace launcher
