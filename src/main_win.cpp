#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#if defined(TRACY_ENABLE)
  #include <tracy/Tracy.hpp>   // ZoneScoped*, FrameMark*, tracy::SetThreadName
#endif

#include "platform/win/WinApp.h"
#include "platform/win/CrashHandler.h"
#include "platform/win/FilesystemWin.h"

using namespace winplat;

// Replace these with your engine hooks
static bool GameInit(WinApp& app) {
    // e.g., set cwd to exe so relative assets work after install
    SetCurrentDirToExe();
    return true;
}
static void GameUpdate(WinApp& app, float dt) {
    (void)dt;
    // your simulation/render kickoffs here
}
static void GameRender(WinApp& app) {
    (void)app;
    // your renderer present path here
}
static void GameResize(WinApp& app, int w, int h, float dpi) {
    (void)app; (void)w; (void)h; (void)dpi;
    // resize swapchain, update ui scale, etc.
}
static void GameShutdown(WinApp& app) {
    (void)app;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("wWinMain");                    // earliest CPU zone
    tracy::SetThreadName("Main Thread");        // readable thread name in Tracy
    FrameMarkStart("Startup");                  // begin discontinuous "Startup" frame
#endif

    // Crash dumps in %LOCALAPPDATA%\ColonyGame\crashdumps
    InstallCrashHandler(CrashConfig{
        .appName = L"ColonyGame",
        .dumpDir = {},            // use default
        .showMessageBox = true
    });

    WinApp app;
    WinCreateDesc desc;
    desc.title          = L"Colony Game";
    desc.clientSize     = { 1600, 900 };
    desc.resizable      = true;
    desc.debugConsole   = true;
    desc.highDPIAware   = true;

    WinApp::Callbacks cbs;
    cbs.onInit     = GameInit;
    cbs.onUpdate   = GameUpdate;
    cbs.onRender   = GameRender;
    cbs.onResize   = GameResize;
    cbs.onShutdown = GameShutdown;
    cbs.onFileDrop = [](WinApp&, const std::vector<std::wstring>& files){
        // Handle dropped files (e.g., load save, config)
        // ...
    };

    if (!app.create(desc, cbs)) {
        return -1;
    }
    return app.run();
}
