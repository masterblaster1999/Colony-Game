// src/app/WinMain.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h>   // For DPI awareness (manifest preferred)
#include <combaseapi.h>
#include <filesystem>
#include "util/Logger.h"
#include "app/AppConfig.h"
#include "platform/win/Window.h"
#include "core/UpdateLoop.h"
#include "render/Renderer.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Prefer manifest to set PMv2, but ensure at runtime if needed:
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);

    // Normalize CWD to the executable directory (fixes “can’t find assets”)
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::current_path(exeDir);

    Logger::init(exeDir / L"logs" / L"colony.log"); // spdlog under the hood

    AppConfig cfg = AppConfig::load(L"assets/config/app.json"); // tolerant defaults

    Window window(hInst, cfg.windowTitle.c_str(), cfg.width, cfg.height);
    window.show();

    Renderer renderer(window.hwnd(), cfg);
    renderer.initialize(); // creates device/swapchain (flip model)

    UpdateLoop loop(1.0/60.0); // fixed 60 Hz updates, interpolated rendering
    loop.run([&](double dt){
        // update(dt);  // deterministic simulation step(s)
        renderer.render(); // render with interpolation if you track alpha
    });

    CoUninitialize();
    return 0;
}
