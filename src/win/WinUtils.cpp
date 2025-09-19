#define WIN32_LEAN_AND_MEAN
#include "WinUtils.h"
#include <vector>

std::filesystem::path GetExecutableDir() {
    DWORD size = 1024;
    std::vector<wchar_t> buf(size);
    DWORD len = 0;
    for (;;) {
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {}; // give up
        if (len < buf.size() - 1) break;
        buf.resize(buf.size() * 2);
    }
    std::filesystem::path exe(buf.data());
    return exe.parent_path();
}

std::wstring GetLastErrorMessage(DWORD err) {
    if (!err) return L"(no error)";
    LPWSTR msg = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring out = len ? msg : L"(unknown error)";
    if (msg) LocalFree(msg);
    return out;
}

void TryHardenDllSearch() {
    // Dynamically resolve to avoid breaking on older Windows (entry point missing).
    auto k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    using Fn = BOOL (WINAPI*)(DWORD);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(k32, "SetDefaultDllDirectories"));
    if (fn) { fn(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); } // ignore failure
}
