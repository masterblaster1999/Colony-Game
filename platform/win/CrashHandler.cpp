#include "CrashHandler.h"

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif

    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>
    #include <ShlObj.h>     // SHGetKnownFolderPath
    #include <string>
    #include <vector>

    #include "CrashDumpWin.h"
    using namespace CrashDumpWin;

    // Resolve %LOCALAPPDATA%\{appName}\Crashes and ensure it exists.
    static std::wstring GetCrashDir(const wchar_t* appName)
    {
        PWSTR localAppDataW = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppDataW)))
        {
            out.assign(localAppDataW);
            CoTaskMemFree(localAppDataW);
            out.append(L"\\");
            out.append(appName && *appName ? appName : L"ColonyGame");
            out.append(L"\\Crashes");
            CreateDirectoryW(out.c_str(), nullptr);
        }
        else
        {
            out.assign(L"."); // fallback to current directory
        }
        return out;
    }

    void InstallCrashHandler(const wchar_t* appName)
    {
        const std::wstring crashDir = GetCrashDir(appName);
        // Configure sensible defaults (all are optional).
        SetDumpLevel(DumpLevel::Balanced);             // detail preset (tiny/small/balanced/heavy/full)
        SetPostCrashAction(PostCrashAction::ExitProcess);
        SetMaxDumpsToKeep(10);                         // keep last N
        SetThrottleSeconds(3);                         // collapse storms
        SetSkipIfDebuggerPresent(true);                // do nothing under debugger
        EnableSidecarMetadata(true);                   // write a small .txt alongside .dmp
        SetExtraCommentLine(L"Crash handler: CrashDumpWin");

        // Initialize the robust crash-dump facility.
        // NOTE: we pass nullptr for buildTag; you can plumb your git hash here if desired.
        Init(appName && *appName ? appName : L"ColonyGame", crashDir.c_str(), /*buildTag*/ nullptr);
    }
#else
    // Non-Windows: no-op to keep any non-Windows tools compiling (repo is Windows-first anyway).
    void InstallCrashHandler(const wchar_t*) {}
#endif
