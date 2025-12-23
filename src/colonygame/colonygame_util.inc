//======================================================================================
// Utilities
//======================================================================================
namespace util {

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring NowStampCompact() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring GetEnv(const wchar_t* name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768) return L"";
    return std::wstring(buf, n);
}

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool EnsureDir(const std::wstring& p) {
    if (DirExists(p)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS || DirExists(p);
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : path.substr(0, pos);
}

static std::wstring Quoted(const std::wstring& s) {
    if (s.find(L' ') != std::wstring::npos || s.find(L'\t') != std::wstring::npos) {
        return L"\"" + s + L"\"";
    }
    return s;
}

static void OpenInExplorer(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::wstring KnownFolderPath(REFKNOWNFOLDERID fid) {
    PWSTR p = nullptr;
    if (SHGetKnownFolderPath(fid, 0, nullptr, &p) != S_OK) return L"";
    std::wstring s = p; CoTaskMemFree(p);
    return s;
}

static std::wstring Format(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    wchar_t buf[4096]; _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap); va_end(ap);
    return buf;
}

template <typename T> static T clamp(T v, T lo, T hi) {
    return std::min(hi, std::max(lo, v));
}

} // namespace util

