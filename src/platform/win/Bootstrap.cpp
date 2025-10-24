// platform/win/Bootstrap.cpp
//
// Windows bootstrap helpers for Colony-Game.
// - No top-of-file macro #defines (use platform/win/WinPredef.h instead)
// - Fixed cg::Log::Info varargs usage for std::string
// - Same behavior as your previous TU: GPU hint exports, DPI config,
//   set current dir to EXE, detect res/, and single-instance guard.

#include "platform/win/WinPredef.h"   // centralizes NOMINMAX + WIN32_LEAN_AND_MEAN + <windows.h>

#include "platform/win/Bootstrap.h"   // declarations for the functions defined here
#include "platform/win/CrashHandler.h"// (optional) if you call InstallCrashHandler elsewhere
#include "core/Log.h"

#include <filesystem>
#include <string>
#include <string_view>

// We call user32 APIs (MessageBoxW, SetProcessDPIAware, SetProcessDpiAwarenessContext, etc.)
#pragma comment(lib, "user32.lib")

// --------------------------------------------------------------------------------------
// High-performance GPU hints (NVIDIA Optimus / AMD PowerXpress)
// Exported globals must be in the exe image to be effective.
// See: GLFW docs on GLFW_USE_HYBRID_HPG & common Windows game guidance.
// --------------------------------------------------------------------------------------
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                  = 0x00000001; // NVIDIA
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;          // AMD
}

namespace cg {

// Prefer discrete GPU where applicable. The exports above do most of the work.
void SelectHighPerformanceGPU() {
  cg::Log::Info("%s", "High-performance GPU hint exported."); // fixed varargs usage
}

// --------------------------------------------------------------------------------------
// DPI configuration
// Try Per-Monitor V2 (Windows 10 Creators Update+), fall back to legacy System DPI.
// Docs: SetProcessDpiAwarenessContext (user32), MS DPI guidance.
// --------------------------------------------------------------------------------------
void ConfigureDPI() {
  // Attempt Per-Monitor V2 at runtime (if OS supports it) without requiring a manifest.
  HMODULE user32 = ::LoadLibraryW(L"user32.dll");
  if (user32) {
    using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setDpiCtx = reinterpret_cast<SetDpiCtxFn>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpiCtx) {
      if (setDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        cg::Log::Info("%s", "DPI awareness: PerMonitorV2");
        ::FreeLibrary(user32);
        return;
      }
    }
    ::FreeLibrary(user32);
  }

  // Fallback for older systems (system DPI aware)
  ::SetProcessDPIAware();
  cg::Log::Info("%s", "DPI awareness: System (fallback)");
}

// --------------------------------------------------------------------------------------
// Set working directory to the executable directory, and prefer local DLLs.
// Returns the exe directory (absolute).
// --------------------------------------------------------------------------------------
std::filesystem::path SetCurrentDirToExe() {
  wchar_t buf[MAX_PATH]{};
  ::GetModuleFileNameW(nullptr, buf, MAX_PATH);

  std::filesystem::path exePath = buf;
  std::filesystem::path exeDir  = exePath.remove_filename();

  // Prefer local DLLs and set the process working directory.
  ::SetDllDirectoryW(exeDir.c_str());
  ::SetCurrentDirectoryW(exeDir.c_str());

  const std::string msg = std::string("Working dir set to: ") + exeDir.string();
  cg::Log::Info("%s", msg.c_str()); // fixed: pass const char* to printf-style logger
  return exeDir;
}

// --------------------------------------------------------------------------------------
// Ensure we can find a "res" directory near the executable (or parent/res for dev trees).
// Returns the resolved resource directory path, or empty() if not found.
// --------------------------------------------------------------------------------------
std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir) {
  auto res = exeDir / "res";
  if (std::filesystem::exists(res)) {
    cg::Log::Info("%s", "res/ folder OK.");
    return res;
  }

  // Optional alternate (e.g., when running from a build tree)
  auto alt = exeDir.parent_path() / "res";
  if (std::filesystem::exists(alt)) {
    cg::Log::Warn("%s", "res/ not next to EXE; using parent/res");
    return alt;
  }

  cg::Log::Error("%s", "res/ folder missing.");
  return {};
}

// --------------------------------------------------------------------------------------
// Basic single-instance guard. Returns a handle if we are the first instance; otherwise
// shows an informational message and returns nullptr.
// --------------------------------------------------------------------------------------
HANDLE CreateSingleInstanceMutex(const wchar_t* name) {
  HANDLE h = ::CreateMutexW(nullptr, FALSE, name);
  if (!h) {
    return nullptr;
  }

  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    ::MessageBoxW(nullptr,
                  L"Colony-Game is already running.",
                  L"Colony-Game",
                  MB_ICONINFORMATION | MB_OK);
    ::CloseHandle(h);
    return nullptr;
  }

  return h;
}

} // namespace cg
