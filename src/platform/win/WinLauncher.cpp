// src/platform/win/WinLauncher.cpp

// These may already be defined via compiler flags or a PCH; guard them to
// avoid C4005 macro redefinition warnings.
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <Windows.h>
#include <ShlObj.h>    // SHGetKnownFolderPath, FOLDERID_LocalAppData
#include <Objbase.h>   // CoTaskMemFree
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <string>
#include <exception>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

// Some older SDKs may not define this; newer ones do in windef.h.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// Ask dGPU on laptops
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                 = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}

static void SetDpiAware()
{
    // Try the modern per-monitor v2 API first.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSet = reinterpret_cast<SetDpiCtxFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        );
        if (pSet)
        {
            pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }

    // Fallback to SHCore's SetProcessDpiAwareness if available.
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore)
    {
        using SetProcessDpiAwarenessFn = HRESULT (WINAPI*)(int);
        auto pSetOld = reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness")
        );
        if (pSetOld)
        {
            // 2 == PROCESS_PER_MONITOR_DPI_AWARE
            pSetOld(2);
        }
        FreeLibrary(shcore);
    }
    else
    {
        // Oldest fallback.
        SetProcessDPIAware();
    }
}

static std::filesystem::path ExeDir()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::filesystem::current_path();

    std::filesystem::path p(buf, buf + n);
    return p.remove_filename();
}

static std::filesystem::path LogDir()
{
    PWSTR localAppData = nullptr;
    std::filesystem::path dir = L".";

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_CREATE,
                                       nullptr,
                                       &localAppData)) &&
        localAppData)
    {
        dir = localAppData;
        CoTaskMemFree(localAppData);
    }

    dir /= L"ColonyGame\\logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static void AppendToLog(std::wstring_view msg)
{
    auto log = LogDir() / L"startup.log";
    std::wofstream f(log, std::ios::app);
    if (!f.is_open())
        return;

    f << msg << L"\n";
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    SetDpiAware();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    // Try to set the thread context as well (if available).
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetThreadDpiCtxFn = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSetThread = reinterpret_cast<SetThreadDpiCtxFn>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext")
        );
        if (pSetThread)
        {
            pSetThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // Ensure working dir is exe dir (fixes asset-relative paths).
    auto exeDir = ExeDir();
    SetCurrentDirectoryW(exeDir.c_str());

    AppendToLog(L"ColonyGame launcher starting…");

    try
    {
        // Sanity-check a critical asset folder:
        if (!std::filesystem::exists(L"assets\\config"))
        {
            MessageBoxW(
                nullptr,
                L"assets\\config not found. Please reinstall or run the game from its install folder.",
                L"ColonyGame – Missing assets",
                MB_ICONERROR | MB_OK
            );
            return 2;
        }

        // Hand off to your existing game main() if you have one:
        extern int GameMain(); // defined in your engine
        return GameMain();
    }
    catch (const std::exception& e)
    {
        wchar_t wmsg[2048];
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, wmsg, 2048);
        AppendToLog(wmsg);
        MessageBoxW(
            nullptr,
            wmsg,
            L"ColonyGame crashed",
            MB_ICONERROR | MB_OK
        );
        return 1;
    }
}
