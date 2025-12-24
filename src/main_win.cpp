#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <DbgHelp.h>
#include <shellapi.h>   // Drag&Drop (WM_DROPFILES)
#include <vector>
#include <string>

#if defined(TRACY_ENABLE)
  #include <tracy/Tracy.hpp>   // ZoneScoped*, FrameMark*, tracy::SetThreadName
#endif

#include "platform/win/WinApp.h"
#include "platform/win/FilesystemWin.h"

using namespace winplat;

#ifdef _MSC_VER
#  pragma comment(lib, "Dbghelp.lib")
#  pragma comment(lib, "Shell32.lib")  // DragQueryFileW / DragFinish
#endif

// -----------------------------------------------------------------------------
// Hybrid-GPU preference (helps laptops pick the discrete GPU)
// -----------------------------------------------------------------------------
extern "C" {
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001; // NVIDIA
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;    // AMD
}

// ---- Runtime bootstrap helpers (Windows-only hardening + DPI) ----------------
namespace {
  // Fallbacks for older SDKs if needed:
  #ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
  #  define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
  #endif
  #ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  #  define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
  #endif
  #ifndef PROCESS_PER_MONITOR_DPI_AWARE
  #  define PROCESS_PER_MONITOR_DPI_AWARE 2
  #endif

  // Try Per-Monitor-V2; fall back to Per-Monitor; then legacy system DPI aware.
  static void EnableModernDPI()
  {
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
      using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);
      if (auto p = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
              ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        if (p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
          return; // success
        }
      }
    }

    if (HMODULE shcore = ::LoadLibraryW(L"shcore.dll")) {
      using SetProcessDpiAwareness_t = HRESULT (WINAPI*)(int /*PROCESS_DPI_AWARENESS*/);
      if (auto p2 = reinterpret_cast<SetProcessDpiAwareness_t>(
              ::GetProcAddress(shcore, "SetProcessDpiAwareness"))) {
        if (SUCCEEDED(p2(PROCESS_PER_MONITOR_DPI_AWARE))) {
          ::FreeLibrary(shcore);
          return; // success
        }
      }
      ::FreeLibrary(shcore);
    }

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
      using SetProcessDPIAware_t = BOOL (WINAPI*)();
      if (auto legacy = reinterpret_cast<SetProcessDPIAware_t>(
              ::GetProcAddress(user32, "SetProcessDPIAware"))) {
        legacy(); // best-effort legacy fallback
      }
    }
  }

  // Optional: name the thread at the OS level for debuggers (Win10+).
  static void SetMainThreadDescription()
  {
    if (HMODULE kernelbase = ::GetModuleHandleW(L"kernelbase.dll")) {
      using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);
      if (auto p = reinterpret_cast<SetThreadDescription_t>(
              ::GetProcAddress(kernelbase, "SetThreadDescription"))) {
        p(::GetCurrentThread(), L"Main Thread");
      }
    }
  }

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

    // 3) Per-Monitor v2 DPI awareness (with fallbacks).
    EnableModernDPI();

    // 4) Helpful for native debuggers and ETW traces.
    SetMainThreadDescription();
  }

  // Returns the DPI for a given window if available; otherwise falls back to LOGPIXELSY or 96.
  static float GetWindowDPI(HWND hwnd)
  {
    UINT dpi = 96;
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
      using GetDpiForWindow_t = UINT (WINAPI*)(HWND);
      if (auto p = reinterpret_cast<GetDpiForWindow_t>(
              ::GetProcAddress(user32, "GetDpiForWindow"))) {
        dpi = p(hwnd); // Available on Win10 1607+ (declared in winuser.h)
      } else {
        // Fallback: query device caps
        if (HDC hdc = ::GetDC(hwnd)) {
          dpi = static_cast<UINT>(::GetDeviceCaps(hdc, LOGPIXELSY));
          ::ReleaseDC(hwnd, hdc);
        }
      }
    }
    return static_cast<float>(dpi);
  }

  // Extract file paths from an HDROP into a vector of std::wstring.
  static std::vector<std::wstring> ExtractDroppedFiles(HDROP hDrop)
  {
    std::vector<std::wstring> files;
    const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    files.reserve(count);
    for (UINT i = 0; i < count; ++i) {
      const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
      // DragQueryFileW writes a NUL terminator. Allocate len+1 to avoid
      // writing past the end of the string buffer.
      std::wstring path;
      path.resize(len + 1, L'\0');
      const UINT copied = ::DragQueryFileW(hDrop, i, path.data(), len + 1);
      // DragQueryFileW returns the number of characters copied, not including
      // the terminating NUL.
      if (copied > 0 && copied <= len) {
        path.resize(copied);
      } else {
        path.resize(len);
      }
      files.emplace_back(std::move(path));
    }
    ::DragFinish(hDrop);
    return files;
  }
}

// ---- Crash dump guard (Windows / MSVC, internal to this TU) ------------------
namespace {

class CrashDumpGuard
{
public:
  explicit CrashDumpGuard(const wchar_t* appName)
    : m_appName(appName)
    , m_prevFilter(nullptr)
  {
    s_instance = this;
    m_prevFilter = ::SetUnhandledExceptionFilter(&CrashDumpGuard::UnhandledExceptionFilter);
  }

  ~CrashDumpGuard()
  {
    ::SetUnhandledExceptionFilter(m_prevFilter);
    s_instance = nullptr;
  }

private:
  static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
  {
    if (!s_instance) {
      return EXCEPTION_EXECUTE_HANDLER;
    }

    // Simple crash-dump folder next to the exe: "crashdumps"
    ::CreateDirectoryW(L"crashdumps", nullptr);

    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    wchar_t fileName[MAX_PATH]{};
    swprintf_s(
      fileName,
      L"crashdumps\\%s_%04u-%02u-%02u_%02u-%02u-%02u.dmp",
      s_instance->m_appName,
      st.wYear, st.wMonth, st.wDay,
      st.wHour, st.wMinute, st.wSecond
    );

    HANDLE hFile = ::CreateFileW(
      fileName,
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );

    if (hFile != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mdei{};
      mdei.ThreadId = ::GetCurrentThreadId();
      mdei.ExceptionPointers = exceptionInfo;
      mdei.ClientPointers = FALSE;

      ::MiniDumpWriteDump(
        ::GetCurrentProcess(),
        ::GetCurrentProcessId(),
        hFile,
        MiniDumpWithIndirectlyReferencedMemory,
        &mdei,
        nullptr,
        nullptr
      );

      ::CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
  }

  const wchar_t*                 m_appName;
  LPTOP_LEVEL_EXCEPTION_FILTER   m_prevFilter;

  static CrashDumpGuard*         s_instance;
};

CrashDumpGuard* CrashDumpGuard::s_instance = nullptr;

} // anonymous namespace

// -----------------------------------------------------------------------------
// Replace these with your engine hooks
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Message-pump driven run loop that fans messages out to cbs
// -----------------------------------------------------------------------------
static int RunMessageLoop(WinApp& app, WinApp::Callbacks& cbs)
{
  // Optional: if WinApp::create() does NOT call onInit, do it here once.
  if (cbs.onInit) {
    cbs.onInit(app);
  }

  LARGE_INTEGER freq{}, last{};
  ::QueryPerformanceFrequency(&freq);
  ::QueryPerformanceCounter(&last);

  bool running = true;
  MSG msg{};
  while (running) {
    // Drain message queue
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }

      switch (msg.message) {
        case WM_SIZE: {
          if (cbs.onResize) {
            const int w = static_cast<int>(LOWORD(msg.lParam));
            const int h = static_cast<int>(HIWORD(msg.lParam));
            const float dpi = GetWindowDPI(msg.hwnd);
            cbs.onResize(app, w, h, dpi);
          }
        } break;

        case WM_DPICHANGED: {
          // wParam: HIWORD/LOWORD contain the new DPI (same in both)
          // lParam: RECT* suggested new window rect for the new DPI
          if (cbs.onResize) {
            RECT* const suggested = reinterpret_cast<RECT*>(msg.lParam);
            // Apps are expected to resize/move to the suggested rect (we don’t reposition here
            // because WinApp likely owns the HWND; this still informs engine about DPI/size).
            const float dpi = static_cast<float>(LOWORD(msg.wParam));
            // If we can read the current client size, pass it along; otherwise use suggested.
            RECT rc{};
            if (::GetClientRect(msg.hwnd, &rc)) {
              cbs.onResize(app, rc.right - rc.left, rc.bottom - rc.top, dpi);
            } else {
              cbs.onResize(app, suggested->right - suggested->left, suggested->bottom - suggested->top, dpi);
            }
          }
        } break;

        case WM_DROPFILES: {
          if (cbs.onFileDrop) {
            const auto files = ExtractDroppedFiles(reinterpret_cast<HDROP>(msg.wParam));
            cbs.onFileDrop(app, files);
          }
        } break;

        default:
          break;
      }

      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }

    // --- Per-frame update & render ---
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    const float dt = static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(freq.QuadPart);
    last = now;

#if defined(TRACY_ENABLE)
    FrameMarkStart("Frame");
#endif

    if (cbs.onUpdate) cbs.onUpdate(app, dt);
    if (cbs.onRender) cbs.onRender(app);

#if defined(TRACY_ENABLE)
    FrameMarkEnd("Frame");
#endif
  }

  if (cbs.onShutdown) cbs.onShutdown(app);
  return 0;
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
#if defined(TRACY_ENABLE)
  ZoneScopedN("wWinMain");                     // earliest CPU zone
  tracy::SetThreadName("Main Thread");         // readable thread name in Tracy
  FrameMarkStart("Startup");                   // begin discontinuous "Startup" frame
#endif

  // OS bootstrap before we touch any windowing or device resources.
  PreBootstrapHardeningAndDpi();

  // Crash dumps in .\crashdumps (one file per crash, timestamped)
  CrashDumpGuard crashGuard{L"ColonyGame"};
  (void)crashGuard; // prevent unused-variable warning under /WX

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
  cbs.onFileDrop = [](WinApp&, const std::vector<std::wstring>& files) {
    (void)files; // Handle dropped files (e.g., load save, config) as needed
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

  // Our own Win32 message pump that forwards messages into cbs and runs update/render.
  return RunMessageLoop(app, cbs);
}
