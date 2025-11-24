// src/Platform/Win/WinLauncher.cpp

// Target at least Windows 10 (required for some DPI APIs we use)
#ifndef WINVER
#   define WINVER 0x0A00
#endif

#ifndef _WIN32_WINNT
#   define _WIN32_WINNT WINVER
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>    // SHGetKnownFolderPath, KF_FLAG_CREATE, FOLDERID_LocalAppData
#include <Objbase.h>   // CoTaskMemFree
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <exception>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

// Ask dGPU on laptops
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static void SetDpiAware()
{
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        using SetProcessDpiAwareness_t = HRESULT(WINAPI*)(int);
        auto f = reinterpret_cast<SetProcessDpiAwareness_t>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (f) f(/*PROCESS_PER_MONITOR_DPI_AWARE*/ 2);
        FreeLibrary(shcore);
    } else {
        // Fallback for older builds of Windows 10
        auto f = reinterpret_cast<BOOL(WINAPI*)(PROCESS_DPI_AWARENESS_CONTEXT)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
        if (f) f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

static std::filesystem::path ExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.remove_filename();
}

static std::filesystem::path LogDir()
{
    PWSTR localAppData = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData);
    std::filesystem::path dir(localAppData ? localAppData : L".");
    CoTaskMemFree(localAppData);
    dir /= L"ColonyGame\\logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static void AppendToLog(std::wstring_view msg)
{
    auto log = LogDir() / L"startup.log";
    std::wofstream f(log, std::ios::app);
    f << msg << L"\n";
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    SetDpiAware();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Ensure working dir is exe dir (fixes "launch errors" due to missing assets)
    auto exeDir = ExeDir();
    SetCurrentDirectoryW(exeDir.c_str());

    AppendToLog(L"ColonyGame starting…");

    try {
        // TODO: Your actual engine/game entry:
        // return RunGame(__argc, __wargv);
        // For now, just sanity-check a critical asset folder:
        if (!std::filesystem::exists(L"assets\\config")) {
            MessageBoxW(nullptr,
                L"assets\\config not found. Please reinstall or run the game from its install folder.",
                L"ColonyGame – Missing assets", MB_ICONERROR | MB_OK);
            return 2;
        }
        // Hand off to your existing game main() if you have one:
        extern int GameMain(); // declare in your engine
        return GameMain();
    } catch (const std::exception& e) {
        wchar_t wmsg[2048];
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, wmsg, 2048);
        AppendToLog(wmsg);
        MessageBoxW(nullptr, wmsg, L"ColonyGame crashed", MB_ICONERROR | MB_OK);
        return 1;
    }
}
