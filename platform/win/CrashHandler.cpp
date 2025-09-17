#pragma once
#define NOMINMAX
#include <windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace colony::win {

inline std::wstring g_dumpDir;

inline void create_dirs(const std::wstring& path) {
    std::error_code ec; std::filesystem::create_directories(path, ec);
}

inline std::wstring timestamp() {
    SYSTEMTIME st; ::GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return buf;
}

inline bool write_minidump(EXCEPTION_POINTERS* ep) {
    if (g_dumpDir.empty()) return false;
    create_dirs(g_dumpDir);
    std::wstring path = g_dumpDir + L"\\crash_" + timestamp() + L".dmp";

    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile,
                                  MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpScanMemory,
                                  ep ? &mei : nullptr, nullptr, nullptr);
    ::CloseHandle(hFile);
    return ok == TRUE;
}

inline LONG WINAPI unhandled(EXCEPTION_POINTERS* ep) {
    write_minidump(ep);
    return EXCEPTION_EXECUTE_HANDLER; // terminate after dump
}

inline void install_crash_handler(const std::wstring& dumpDir) {
    g_dumpDir = dumpDir;
    ::SetUnhandledExceptionFilter(&unhandled);
}

} // namespace colony::win
