#pragma once
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <filesystem>
#include <string>

#pragma comment(lib, "Shell32.lib")

namespace colony::win {

inline std::wstring exe_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, buf + n);
}

inline std::wstring exe_directory() {
    std::filesystem::path p = exe_path();
    return p.parent_path().wstring();
}

inline std::wstring known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) {
        std::wstring result = path;
        ::CoTaskMemFree(path);
        return result;
    }
    return L".";
}

inline void set_cwd_to_exe() {
    ::SetCurrentDirectoryW(exe_directory().c_str());
}

inline void create_dirs(const std::wstring& dir) {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
}

} // namespace colony::win
