// Minimal crash-dump writer for Windows (x64).
// Links: dbghelp.lib (CMake does this in CGGameTarget for the exe).
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <vector>
#include <shlobj.h> // SHGetKnownFolderPath

#pragma comment(lib, "dbghelp.lib")

namespace {
    std::filesystem::path SavedGamesDir(const wchar_t* appName) {
        PWSTR path = nullptr;
        std::filesystem::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &path))) {
            out = std::filesystem::path(path) / appName / L"Crashes";
            CoTaskMemFree(path);
        }
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
        return out;
    }

    LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* ep) {
        const auto dir = SavedGamesDir(L"Colony Game");
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t fileName[128]{};
        swprintf_s(fileName, L"%04u%02u%02u-%02u%02u%02u.dmp",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        const auto dumpPath = dir / fileName;
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;

            // Include modules & memory info for better triage without giant dumps.
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                              (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
                                              MiniDumpWithDataSegs |
                                              MiniDumpWithThreadInfo),
                              &mei, nullptr, nullptr);
            CloseHandle(hFile);
        }
        // Let the default unhandled exception filter do its thing after writing the dump.
        return EXCEPTION_CONTINUE_SEARCH;
    }
} // namespace

namespace wincrash {
    void InitCrashHandler(const wchar_t* /*appName*/) {
        // Install a vectored exception handler early.
        AddVectoredExceptionHandler(1 /*first*/, VectoredHandler);
    }
} // namespace wincrash
