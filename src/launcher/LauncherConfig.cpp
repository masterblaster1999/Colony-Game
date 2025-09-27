#define WIN32_LEAN_AND_MEAN
#include "LauncherConfig.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <cwchar>

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
}

namespace launcher {
std::wstring read_target_exe() {
    const fs::path cfg = winenv::resource_dir() / "launcher.cfg";
    std::wifstream in(cfg);
    if (in) {
        std::wstring line;
        std::getline(in, line);
        // Strip UTF-8/UTF-16 BOMs best-effort
        if (!line.empty() && (line[0] == 0xFEFF)) line.erase(0, 1);
        trim(line);
        if (!line.empty() && line[0] != L'#' && line.rfind(L"//", 0) == std::wstring::npos)
            return line;
    }
    return L"ColonyGame.exe";
}
}
