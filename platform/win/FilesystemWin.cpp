#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "FilesystemWin.h"

#include <fstream>

namespace winplat {

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

fs::path GetExecutablePath()
{
    wchar_t buf[MAX_PATH]{};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, buf + n));
}

fs::path GetExecutableDir()
{
    auto p = GetExecutablePath();
    return p.remove_filename();
}

bool SetCurrentDirToExe()
{
    std::error_code ec;
    fs::current_path(GetExecutableDir(), ec);
    return !ec;
}

fs::path KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) {
        out = fs::path(p);
        ::CoTaskMemFree(p);
    }
    return out;
}

bool EnsureDir(const fs::path& dir)
{
    std::error_code ec;
    if (dir.empty()) return false;
    if (fs::exists(dir, ec)) return true;
    return fs::create_directories(dir, ec);
}

fs::path AppDataRoot(const std::wstring& appName)
{
    auto base = KnownFolder(FOLDERID_LocalAppData);
    auto dir = base / appName;
    EnsureDir(dir);
    return dir;
}

fs::path LogsDir(const std::wstring& appName)
{
    auto dir = AppDataRoot(appName) / L"logs";
    EnsureDir(dir);
    return dir;
}

fs::path SavesDir(const std::wstring& appName)
{
    auto dir = AppDataRoot(appName) / L"saves";
    EnsureDir(dir);
    return dir;
}

fs::path ConfigDir(const std::wstring& appName)
{
    auto dir = AppDataRoot(appName) / L"config";
    EnsureDir(dir);
    return dir;
}

fs::path CrashDumpDir(const std::wstring& appName)
{
    auto dir = AppDataRoot(appName) / L"crashdumps";
    EnsureDir(dir);
    return dir;
}

bool ReadFileBinary(const fs::path& p, std::vector<uint8_t>& outData)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize size = f.tellg();
    if (size < 0) return false;
    outData.resize(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(outData.data()), size)) return false;
    return true;
}

bool WriteFileBinary(const fs::path& p, const uint8_t* data, size_t size)
{
    std::error_code ec;
    if (!EnsureDir(p.parent_path())) return false;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

std::wstring Win32ErrorMessage(DWORD err)
{
    LPWSTR buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = ::FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s = (len && buf) ? std::wstring(buf, buf + len) : L"";
    if (buf) ::LocalFree(buf);
    return s;
}

} // namespace winplat
