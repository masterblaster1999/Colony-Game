// src/AppMain.cpp
//
// Centralized Windows header policy: defines WIN32_LEAN_AND_MEAN, NOMINMAX, STRICT
// and includes <windows.h> safely once for the entire project.

#include "platform/win/WinCommon.h"

#include "platform/win/LauncherLogSingletonWin.h" // LauncherLog(), WriteLog()

#include "app/CommandLineArgs.h"

#include "AppWindow.h"
#include "CrashDump.h"
#include "UserSettings.h"

#include "platform/win/PathUtilWin.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#   define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

namespace
{
    void HardenDllSearch()
    {
        // Windows 8+ / KB2533623: restrict default DLL search path to safe locations.
        // If not present (older systems), this call will simply fail and we ignore it.
        using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);

        if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
        {
            auto pSetDefaultDllDirectories =
                reinterpret_cast<SetDefaultDllDirectories_t>(
                    ::GetProcAddress(k32, "SetDefaultDllDirectories"));

            if (pSetDefaultDllDirectories)
            {
                // 0x00001000 == LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                (void)pSetDefaultDllDirectories(0x00001000);
            }
        }
    }

    void ApplyDpiAwareness()
    {
        // Microsoft recommends declaring PMv2 DPI in the app manifest.
        // This runtime path is a safe fallback as long as it runs BEFORE any HWND creation.
        // Try Per-Monitor V2 first (Win10+), then fall back to system DPI aware (Vista+).

        if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
        {
            using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);

            auto pSetProcessDpiAwarenessContext =
                reinterpret_cast<SetProcessDpiAwarenessContext_t>(
                    ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

            if (pSetProcessDpiAwarenessContext)
            {
                if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                    return; // success
            }
        }

        // Fallback: system DPI aware (Vista+). Preferred solution is a manifest.
        ::SetProcessDPIAware();
    }

    void NameMainThread()
    {
        // Give the main thread a descriptive name for VS/WinDbg/WPA.
        using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);

        if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
        {
            auto pSetThreadDescription =
                reinterpret_cast<SetThreadDescription_t>(
                    ::GetProcAddress(k32, "SetThreadDescription"));

            if (pSetThreadDescription)
                (void)pSetThreadDescription(::GetCurrentThread(), L"Main");
        }
    }
} // anonymous namespace

// Renamed from wWinMain to a normal function so that EntryWinMain.cpp
// can be the sole Windows entry point and delegate to this function.
int GameMain(HINSTANCE hInstance, PWSTR cmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(cmdLine);

    auto& log = LauncherLog();
    WriteLog(log, L"[AppMain] GameMain starting.");

    HardenDllSearch();
    ApplyDpiAwareness();
    InstallCrashHandler(L"ColonyGame");
    NameMainThread();

    // ----- Command line parsing / recovery -----
    const colony::appwin::CommandLineArgs args = colony::appwin::ParseCommandLineArgs();

    if (args.showHelp)
    {
        const std::wstring help = colony::appwin::BuildCommandLineHelpText();
        MessageBoxW(nullptr, help.c_str(), L"Colony Game - Command Line", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (!args.unknown.empty())
    {
        std::wstring msg = L"Unknown command line option(s):\n";
        for (const auto& u : args.unknown)
        {
            msg += L"  " + u + L"\n";
        }
        msg += L"\n";
        msg += colony::appwin::BuildCommandLineHelpText();
        MessageBoxW(nullptr, msg.c_str(), L"Colony Game - Unknown option", MB_OK | MB_ICONWARNING);
        return -1;
    }

    auto tryDelete = [&](const fs::path& p, const wchar_t* label) {
        if (p.empty())
            return;
        std::error_code ec;
        const bool existed = fs::exists(p, ec) && !ec;
        if (existed)
        {
            WriteLog(log, std::wstring(L"[AppMain] Deleting ") + label + L": " + p.wstring());
            fs::remove(p, ec);
            if (ec)
            {
                const std::wstring err = std::wstring(L"Failed to delete ") + label + L"\n\n" + p.wstring() + L"\n\n" +
                                         L"Error: " + std::to_wstring(ec.value()) + L"\n";
                MessageBoxW(nullptr, err.c_str(), L"Colony Game", MB_OK | MB_ICONWARNING);
            }
        }
    };

    if (args.resetSettings)
        tryDelete(colony::appwin::UserSettingsPath(), L"settings.json");

    if (args.resetImGui)
    {
        const fs::path dataDir = colony::appwin::winpath::writable_data_dir();
        if (!dataDir.empty())
            tryDelete(dataDir / L"imgui.ini", L"imgui.ini");
    }

    WriteLog(log, L"[AppMain] Creating AppWindow...");

    AppWindow::CreateOptions opt{};
    opt.width  = args.width.value_or(1280);
    opt.height = args.height.value_or(720);

    // Safe-mode: ignore settings + ImGui ini and do NOT write settings back out.
    if (args.safeMode)
    {
        opt.ignoreUserSettings    = true;
        opt.settingsWriteEnabled  = false;
        opt.disableImGuiIni       = true;
    }

    if (args.ignoreSettings)
        opt.ignoreUserSettings = true;
    if (args.ignoreImGuiIni)
        opt.disableImGuiIni = true;
    if (args.disableImGui)
        opt.disableImGui = true;

    // Overrides (tri-state)
    opt.fullscreen = args.fullscreen;
    opt.vsync      = args.vsync;
    opt.rawMouse   = args.rawMouse;

    opt.maxFrameLatency     = args.maxFrameLatency;
    opt.maxFpsWhenVsyncOff  = args.maxFpsWhenVsyncOff;
    opt.pauseWhenUnfocused  = args.pauseWhenUnfocused;
    opt.maxFpsWhenUnfocused = args.maxFpsWhenUnfocused;

    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, opt))
    {
        WriteLog(log, L"[AppMain] AppWindow.Create FAILED");
        return -1;
    }

    const int exitCode = app.MessageLoop();
    WriteLog(log, L"[AppMain] MessageLoop exited code=" + std::to_wstring(exitCode));
    return exitCode;
}
