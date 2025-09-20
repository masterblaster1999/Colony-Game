#include "Log.h"
#include <windows.h>
#include <mutex>
#include <fstream>
#include <filesystem>

namespace logsys {
    static std::wofstream g_file;
    static std::mutex g_mu;

    static std::wstring Now() {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buf;
    }

    void Init() {
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
        // Rotate previous log for cleanliness
        if (std::filesystem::exists("logs/game.log", ec)) {
            std::filesystem::rename("logs/game.log", "logs/game.prev.log", ec);
        }
        g_file.open(L"logs\\game.log", std::ios::out | std::ios::trunc);
        Write(L"[Log] initialized");
    }

    void Write(const wchar_t* s) {
        std::lock_guard<std::mutex> lock(g_mu);
        std::wstring line = Now(); line += L" "; line += s; line += L"\n";
        if (g_file.is_open()) { g_file << line; g_file.flush(); }
        OutputDebugStringW(line.c_str()); // appears in VS Output window. :contentReference[oaicite:8]{index=8}
    }

    std::wstring FormatLastError(DWORD e) {
        if (!e) e = GetLastError();
        LPWSTR buf = nullptr;
        DWORD len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, e, 0 /*LANG_NEUTRAL*/,
                                   (LPWSTR)&buf, 0, nullptr);
        std::wstring s = (len && buf) ? std::wstring(buf, buf + len) : L"(unknown)";
        if (buf) LocalFree(buf);
        return s;
    }
}
