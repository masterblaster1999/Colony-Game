#include "Crash.h"
#include "Log.h"
#include <windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <ctime>
#include <filesystem>   // <-- needed for std::filesystem::path/ops
#include <system_error>

#pragma comment(lib, "Dbghelp.lib")

namespace core {

static LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS* info) {
    // Timestamped dump name
    std::time_t t = std::time(nullptr);
    std::tm tmLocal{};
    localtime_s(&tmLocal, &t);
    wchar_t name[128];
    wcsftime(name, 128, L"crash-%Y%m%d-%H%M%S.dmp", &tmLocal);

    // Default to %LOCALAPPDATA%\ColonyGame\logs (same as logs)
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path base = std::filesystem::path(exePath).parent_path();

    // If we stashed a dump dir in TLS, use it (set by InstallCrashHandler).
    // Simple approach: read from process environment (set by InstallCrashHandler).
    wchar_t dumpDirBuf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"CG_DUMP_DIR", dumpDirBuf, MAX_PATH);
    std::filesystem::path outDir = (n > 0) ? std::filesystem::path(dumpDirBuf) : base;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    std::filesystem::path dumpPath = outDir / name;

    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("Unhandled exception. Failed to create minidump file %s",
                  std::string(dumpPath.string()).c_str());
        return EXCEPTION_EXECUTE_HANDLER;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;

    // Build a proper MINIDUMP_TYPE bitmask (fixes C2664).
    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory
    );

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), hFile,
        dumpType,
        info ? &mei : nullptr, nullptr, nullptr);

    CloseHandle(hFile);

    LOG_ERROR("Unhandled exception. Minidump %s: %s",
              std::string(dumpPath.string()).c_str(), ok ? "OK" : "FAILED");

    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler(const std::filesystem::path& dumpDir) {
    // Not strictly recommended to dump from within the crashing process,
    // but widely used in practice; for full robustness, spawn a helper.
    SetUnhandledExceptionFilter(&UnhandledExceptionFilterFn);
    // Stash dump dir in env for the filter
    SetEnvironmentVariableW(L"CG_DUMP_DIR", dumpDir.wstring().c_str());
}

} // namespace core
