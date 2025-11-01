#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include <string>

#if defined(TRACY_ENABLE)
  #include <tracy/Tracy.hpp>   // ZoneScoped*, FrameMark*, tracy::SetThreadName
#endif

#include "platform/win/WinApp.h"
#include "platform/win/CrashHandler.h"
#include "platform/win/FilesystemWin.h"

using namespace winplat;

// ---- Runtime bootstrap helpers (Windows-only hardening + DPI) ----------------
namespace {
  // Fallbacks for older SDKs if needed:
  #ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
  #  define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
  #endif
  #ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  #  define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
  #endif

  static void PreBootstrapHardeningAndDpi()
  {
    // 1) Safer DLL search path (mitigates current-dir DLL hijacking). Runtime-checked.
    if (HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll")) {
      using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);
      if (auto p = reinterpret_cast<SetDefaultDllDirectories_t>(
              ::GetProcAddress(kernel32, "SetDefaultDllDirectories"))) {
        p(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); // ok if it fails on older OS
      }
    }

    // 2) Terminate on heap corruption (recommended, no-ops on very old systems).
    ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    // 3) Per-Monitor v2 DPI awareness if available (kept robust via runtime check).
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
      using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);
      if (auto p = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
              ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        (void)p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // ignore failure on older OS
      }
    }
  }
}

// Replace these with your engine hooks
static bool GameInit(WinApp& /*app*/) {
  // e.g., set cwd to exe so relative assets work after install
  SetCurrentDirToExe();
  return true;
}
static void GameUpdate(WinApp& /*app*/, float /*dt*/) {
  // your simulation/render kickoffs here
}
static void GameRender(WinApp& /*app*/) {
  // your renderer present path here
}
static void GameResize(WinApp& /*app*/, int /*w*/, int /*h*/, float /*dpi*/) {
  // resize swapchain, update ui scale, etc.
}
static void GameShutdown(WinApp& /*app*/) {}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
#if defined(TRACY_ENABLE)
  ZoneScopedN("wWinMain");                     // earliest CPU zone
  tracy::SetThreadName("Main Thread");         // readable thread name in Tracy
  FrameMarkStart("Startup");                   // begin discontinuous "Startup" frame
#endif

  // OS bootstrap before we touch any windowing or device resources.
  PreBootstrapHardeningAndDpi();

  // Crash dumps in %LOCALAPPDATA%\ColonyGame\crashdumps
  winplat::InstallCrashHandler(L"ColonyGame"); // fully-qualified; matches header

  WinApp app;
  WinCreateDesc desc;
  desc.title        = L"Colony Game";
  desc.clientSize   = { 1600, 900 };
  desc.resizable    = true;
  desc.debugConsole = true;
  desc.highDPIAware = true;

  WinApp::Callbacks cbs;
  cbs.onInit     = GameInit;
  cbs.onUpdate   = GameUpdate;
  cbs.onRender   = GameRender;
  cbs.onResize   = GameResize;
  cbs.onShutdown = GameShutdown;
  cbs.onFileDrop = [](WinApp&, const std::vector<std::wstring>& /*files*/) {
    // Handle dropped files (e.g., load save, config) ...
  };

  if (!app.create(desc, cbs)) {
#if defined(TRACY_ENABLE)
    FrameMarkEnd("Startup");
#endif
    return -1;
  }

#if defined(TRACY_ENABLE)
  FrameMarkEnd("Startup"); // close the discontinuous "Startup" frame
#endif
  return app.run();
}
