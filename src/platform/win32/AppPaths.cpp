#include "AppPaths.h"
#include <windows.h>
#include <shlobj_core.h> // SHGetKnownFolderPath
#include <filesystem>
#include <stdexcept>

using namespace std;

namespace fs = std::filesystem;

namespace winqol {
    static wstring getKnownFolder(REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        if (S_OK != SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) {
            throw runtime_error("SHGetKnownFolderPath failed");
        }
        wstring path(p);
        CoTaskMemFree(p);
        return path;
    }

    void EnsureDir(const wstring& path) {
        fs::create_directories(path);
    }

    wstring ExeDir() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0) return L".";
        fs::path p(buf);
        return p.remove_filename().wstring();
    }

    wstring AppDataRoot(const wstring& appName) {
        wstring root = getKnownFolder(FOLDERID_LocalAppData) + L"\\" + appName;
        EnsureDir(root);
        return root;
    }
    wstring LogsDir(const wstring& appName) {
        auto p = AppDataRoot(appName) + L"\\logs";
        EnsureDir(p); return p;
    }
    wstring DumpsDir(const wstring& appName) {
        auto p = AppDataRoot(appName) + L"\\dumps";
        EnsureDir(p); return p;
    }
}
