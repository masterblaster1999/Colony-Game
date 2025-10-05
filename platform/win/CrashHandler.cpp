#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "CrashHandler.h"

#include <DbgHelp.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

namespace {

using namespace std::chrono;

winplat::CrashConfig g_cfg{};
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

std::wstring TimeStampUTC()
{
    SYSTEMTIME st{};
    ::GetSystemTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::filesystem::path MakeDumpPath(const winplat::CrashConfig& cfg)
{
    std::filesystem::path dir = cfg.dumpDir.empty()
        ? winplat::GetDefaultCrashDumpDir(cfg.appName)
        : cfg.dumpDir;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    DWORD pid = ::GetCurrentProcessId();
    DWORD tid = ::GetCurrentThreadId();

    std::wostringstream oss;
    oss << cfg.appName << L"_" << TimeStampUTC()
        << L"_pid" << pid << L"_tid" << tid << L".dmp";
    return dir / oss.str();
}

LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* info)
{
    auto dumpPath = MakeDumpPath(g_cfg);
    winplat::WriteMiniDump(dumpPath, info);

    if (g_cfg.showMessageBox) {
        std::wstring msg = L"A crash dump was written to:\n\n" + dumpPath.wstring();
        ::MessageBoxW(nullptr, msg.c_str(), g_cfg.appName.c_str(), MB_ICONERROR | MB_OK);
    }

    if (g_prevFilter) {
        return g_prevFilter(info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetupCRTHandlers()
{
    // Avoid Windows error boxes blocking CI/users
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Invalid parameter handler
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
        ::RaiseException(EXCEPTION_INVALID_PARAMETER, 0, 0, nullptr);
    });

    // Pure virtual call handler
    _set_purecall_handler([]() {
        ::RaiseException(EXCEPTION_PURE_VIRTUAL_CALL, 0, 0, nullptr);
    });

    // New handler (out of memory)
    _set_new_handler([](size_t) -> int {
        ::RaiseException(EXCEPTION_NO_MEMORY, 0, 0, nullptr);
        return 0;
    });
}

} // anonymous

namespace winplat {

std::filesystem::path GetDefaultCrashDumpDir(const std::wstring& appName)
{
    // %LOCALAPPDATA%\appName\crashdumps
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
        result = std::filesystem::path(path) / appName / L"crashdumps";
        ::CoTaskMemFree(path);
    } else {
        result = std::filesystem::temp_directory_path() / appName / L"crashdumps";
    }
    return result;
}

void InstallCrashHandler(const CrashConfig& cfg)
{
    g_cfg = cfg;
    if (g_cfg.dumpDir.empty()) {
        g_cfg.dumpDir = GetDefaultCrashDumpDir(g_cfg.appName);
    }
    SetupCRTHandlers();
    g_prevFilter = ::SetUnhandledExceptionFilter(TopLevelFilter);
}

bool WriteMiniDump(const std::filesystem::path& dumpPath, EXCEPTION_POINTERS* exceptionPointers)
{
    HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exInfo{};
    exInfo.ThreadId          = ::GetCurrentThreadId();
    exInfo.ExceptionPointers = exceptionPointers;
    exInfo.ClientPointers    = FALSE;

    BOOL ok = ::MiniDumpWriteDump(
        ::GetCurrentProcess(),
        ::GetCurrentProcessId(),
        hFile,
        MiniDumpWithFullMemory, // adjust if you prefer smaller dumps
        exceptionPointers ? &exInfo : nullptr,
        nullptr,
        nullptr);

    ::CloseHandle(hFile);
    return ok == TRUE;
}

} // namespace winplat
