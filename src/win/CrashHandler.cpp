// src/win/CrashHandler.cpp  (full file; Windows-only)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>     // SHGetKnownFolderPath
#include <filesystem>
#include <string>
#include <vector>
#include <new>          // std::set_new_handler
#include <cstdio>
#include <cstdint>

#pragma comment(lib, "Dbghelp.lib")

namespace crash {

namespace fs = std::filesystem;
static const wchar_t* g_appName = L"ColonyGame";
static fs::path       g_dumpDir;

static fs::path EnsureDumpDir() {
    if (!g_dumpDir.empty()) return g_dumpDir;

    PWSTR lad = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &lad))) {
        fs::path base(lad);
        CoTaskMemFree(lad);
        fs::path dir = base / L"ColonyGame" / L"crashdumps";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return g_dumpDir = dir;
    }
    // Fallback: %TEMP%\ColonyGame\crashdumps
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    fs::path dir(tmp);
    dir /= L"ColonyGame\\crashdumps";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return g_dumpDir = dir;
}

static std::wstring NowStamp() {
    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04u-%02u-%02u_%02u-%02u-%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static void AppendLine(std::wstring& s, const wchar_t* key, const wchar_t* value) {
    s.append(key).append(L"=").append(value).append(L"\r\n");
}
static void AppendLine(std::wstring& s, const wchar_t* key, uint32_t value) {
    wchar_t buf[32]; _itow_s(static_cast<int>(value), buf, 10);
    s.append(key).append(L"=").append(buf).append(L"\r\n");
}
static void AppendLine(std::wstring& s, const wchar_t* key, uint64_t value) {
    wchar_t buf[64]; _ui64tow_s(value, buf, 10); // value, buffer, radix
    s.append(key).append(L"=").append(buf).append(L"\r\n");
}

// Small helper: choose a richer minidump safely
static MINIDUMP_TYPE ChooseDumpType(bool include_scans, bool include_handles, bool include_threadinfo) {
    int flags = MiniDumpNormal | MiniDumpWithUnloadedModules | MiniDumpWithDataSegs;
    if (include_handles)    flags |= MiniDumpWithHandleData;
    if (include_scans)      flags |= MiniDumpScanMemory;
    if (include_threadinfo) flags |= MiniDumpWithThreadInfo;
    return static_cast<MINIDUMP_TYPE>(flags); // explicit cast fixes C2440
}

// Optional: include some basic system info in a sidecar .txt
static void WriteCrashInfoTxt(const fs::path& dmpPath, EXCEPTION_POINTERS* ep) {
    std::wstring text;
    OSVERSIONINFOEXW ver{}; ver.dwOSVersionInfoSize = sizeof(ver);
    ::GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&ver));
    SYSTEM_INFO si{}; GetSystemInfo(&si);

    AppendLine(text, L"app", g_appName);
    AppendLine(text, L"os_major", ver.dwMajorVersion);
    AppendLine(text, L"os_minor", ver.dwMinorVersion);
    AppendLine(text, L"os_build", ver.dwBuildNumber);
    AppendLine(text, L"cpu_count", static_cast<uint32_t>(si.dwNumberOfProcessors));
    if (ep && ep->ExceptionRecord) {
        AppendLine(text, L"exception_code", static_cast<uint64_t>(ep->ExceptionRecord->ExceptionCode));
        AppendLine(text, L"exception_flags", static_cast<uint64_t>(ep->ExceptionRecord->ExceptionFlags));
    }

    fs::path txt = dmpPath; txt.replace_extension(L".txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, txt.c_str(), L"wb, ccs=UTF-8") == 0 && f) {
        fputws(text.c_str(), f);
        fclose(f);
    }
}

struct CrashConfig {
    const wchar_t* appDisplayName = L"ColonyGame";
    const wchar_t* customDumpDir  = nullptr;
    bool guiMessage = true;
    bool scanMemory = true;
    bool captureHandles = true;
    bool captureThreadInfo = true;
};

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    const fs::path dir = EnsureDumpDir();
    const fs::path file = dir / (std::wstring(g_appName) + L"_" + NowStamp() + L".dmp");

    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        const MINIDUMP_TYPE type = ChooseDumpType(/*scan*/true, /*handles*/true, /*threads*/true);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, type, &mei, nullptr, nullptr);
        CloseHandle(hFile);

        WriteCrashInfoTxt(file, ep);
    }

    MessageBoxW(nullptr,
        L"Colony‑Game hit a fatal error and saved a crash report.\n\n"
        L"Please send the newest *.dmp (and .txt) from:\n"
        L"%LOCALAPPDATA%\\ColonyGame\\crashdumps",
        g_appName, MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void __cdecl NewFailureHandler() {
    // Force a clean, actionable crash dump on bad_alloc storms
    RaiseException(0xE000C0DE, 0, 0, nullptr);
}

bool InstallCrashHandler(const CrashConfig& cfg) {
    if (cfg.appDisplayName && *cfg.appDisplayName) g_appName = cfg.appDisplayName;
    if (cfg.customDumpDir && *cfg.customDumpDir) {
        g_dumpDir = fs::path(cfg.customDumpDir);
        std::error_code ec; fs::create_directories(g_dumpDir, ec);
    }
    std::set_new_handler(NewFailureHandler);

    // Put our SEH filter in place. Note: Not all C++ exceptions will reach here on
    // all CRTs/OS combinations; for broader coverage consider a vectored handler too.
    SetUnhandledExceptionFilter(&TopLevelFilter);
    return true;
}

// Back‑compat overload used by existing code
bool InstallCrashHandler(const wchar_t* appDisplayName, const wchar_t* dumpDir) {
    CrashConfig cfg{};
    cfg.appDisplayName = appDisplayName;
    cfg.customDumpDir  = dumpDir;
    return InstallCrashHandler(cfg);
}

} // namespace crash

// Export a C‑style shim if other TUs expect this symbol without the namespace.
extern "C" __declspec(dllexport) bool InstallCrashHandler(const wchar_t* appDisplayName, const wchar_t* dumpDir) {
    return crash::InstallCrashHandler(appDisplayName, dumpDir);
}
