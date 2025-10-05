--- a/WinLauncher.cpp
+++ b/WinLauncher.cpp
@@
-#ifndef UNICODE
-#define UNICODE
-#endif
-#ifndef _UNICODE
-#define _UNICODE
-#endif
-#ifndef WIN32_LEAN_AND_MEAN
-#define WIN32_LEAN_AND_MEAN
-#endif
-#ifndef NOMINMAX
-#define NOMINMAX
-#endif
-#include <windows.h>
-#include <shellapi.h> // CommandLineToArgvW
-#include <string>
-#include <vector>
-#include <filesystem>
-#include <fstream>
-#include <sstream>
-#include <iomanip>
-#include <chrono>
-#include <cwctype>   // iswspace
-#include <ctime>     // localtime_s
-#include <cstdio>    // freopen_s
+#ifndef UNICODE
+#define UNICODE
+#endif
+#ifndef _UNICODE
+#define _UNICODE
+#endif
+#ifndef WIN32_LEAN_AND_MEAN
+#define WIN32_LEAN_AND_MEAN
+#endif
+#ifndef NOMINMAX
+#define NOMINMAX
+#endif
+#include <windows.h>
+#include <shellapi.h> // CommandLineToArgvW
+#include <string>
+#include <vector>
+#include <filesystem>
+#include <fstream>
+#include <sstream>
+#include <iomanip>
+#include <chrono>
+#include <cwctype>   // iswspace
+#include <ctime>     // localtime_s
+#include <cstdio>    // freopen_s
@@
-#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
-#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
-#endif
+#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
+#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
+#endif
@@
-// NEW: centralize Windows path/CWD logic in one place.
+// NEW: centralize Windows path/CWD logic in one place.
 #include "platform/win/PathUtilWin.h"
 // NEW: Minimal wiring for a fixed‑timestep loop (optional embedded mode)
 #include "core/FixedTimestep.h"
 // NEW: Crash handler (minidumps) — initialize at process start in wWinMain.
 #include "platform/win/CrashHandlerWin.h"
 
 namespace fs = std::filesystem;
 
 // ---------- Utilities ----------
 static std::wstring LastErrorMessage(DWORD err = GetLastError())
 {
     LPWSTR msg = nullptr;
     DWORD len = FormatMessageW(
         FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
         nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
     std::wstring out = (len && msg) ? msg : L"";
     if (msg) LocalFree(msg);
     return out;
 }
 
 // NEW: Fail‑fast on heap corruption for improved crash diagnosability.
 static void EnableHeapTerminationOnCorruption()
 {
     HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
     if (hKernel)
     {
         using HeapSetInformation_t = BOOL (WINAPI *)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);
         auto pHeapSetInformation = reinterpret_cast<HeapSetInformation_t>(GetProcAddress(hKernel, "HeapSetInformation"));
         if (pHeapSetInformation)
         {
             pHeapSetInformation(GetProcessHeap(), HeapEnableTerminationOnCorruption, nullptr, 0);
         }
     }
 }
 
-// Restrict DLL search order to safe defaults and remove CWD from search path.
-// Dynamically resolves SetDefaultDllDirectories for broad OS/SDK compatibility.
-static void EnableSafeDllSearch()
-{
-    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
-    if (hKernel32)
-    {
-        using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
-        auto pSetDefaultDllDirectories = reinterpret_cast<PFN_SetDefaultDllDirectories>(
-            GetProcAddress(hKernel32, "SetDefaultDllDirectories"));
-        if (pSetDefaultDllDirectories)
-        {
-            // Only search system locations, the application directory, and explicitly added dirs.
-            pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
-        }
-    }
-    // Explicitly remove the current directory from the search path.
-    // (Pass L"" to remove CWD; passing NULL would *restore* default search order.)
-    SetDllDirectoryW(L"");
-}
+// Restrict DLL search order to safe defaults and remove CWD from search path.
+// Dynamically resolves SetDefaultDllDirectories for broad OS/SDK compatibility.
+// Note: LOAD_LIBRARY_SEARCH_DEFAULT_DIRS includes the application directory, System32,
+// and any user directories added with AddDllDirectory/SetDllDirectory. :contentReference[oaicite:2]{index=2}
+static void EnableSafeDllSearch()
+{
+    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
+    if (hKernel32)
+    {
+        using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
+        auto pSetDefaultDllDirectories =
+            reinterpret_cast<PFN_SetDefaultDllDirectories>(GetProcAddress(hKernel32, "SetDefaultDllDirectories"));
+        if (pSetDefaultDllDirectories)
+        {
+            // Establish the modern, safer default directories for DLL resolution.
+            (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
+        }
+
+        // Optional: explicitly add the application directory as a user dir (redundant when
+        // DEFAULT_DIRS is active, but harmless and future‑proof). Only if AddDllDirectory exists.
+        using PFN_AddDllDirectory = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
+        auto pAddDllDirectory =
+            reinterpret_cast<PFN_AddDllDirectory>(GetProcAddress(hKernel32, "AddDllDirectory"));
+        if (pAddDllDirectory)
+        {
+            const fs::path exeDir = winpath::exe_dir();
+            if (!exeDir.empty())
+            {
+                (void)pAddDllDirectory(exeDir.c_str());
+            }
+        }
+    }
+
+    // Explicitly remove the current directory from the search path.
+    // (Passing L"" removes CWD; passing NULL would restore the legacy order.) :contentReference[oaicite:3]{index=3}
+    SetDllDirectoryW(L"");
+}
 
 // NEW: High‑DPI awareness (Per‑Monitor‑V2 if available, else system‑DPI).
 static void EnableHighDpiAwareness()
 {
     HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
     if (hUser32)
     {
         using PFN_SetProcessDpiAwarenessContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
         auto pSetCtx = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
             GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
         if (pSetCtx)
         {
             // Best default for desktop games on modern Windows
             if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
             {
                 return;
             }
         }
         using PFN_SetProcessDPIAware = BOOL (WINAPI*)(void);
         auto pSetAware = reinterpret_cast<PFN_SetProcessDPIAware>(GetProcAddress(hUser32, "SetProcessDPIAware"));
         if (pSetAware) pSetAware(); // fallback to system DPI awareness
     }
 }
 
@@
-// Simple file logger under %LOCALAPPDATA%\ColonyGame\logs
-static fs::path LogsDir()
-{
-    fs::path out = winpath::writable_data_dir() / L"logs";
-    std::error_code ec;
-    fs::create_directories(out, ec);
-    return out;
-}
-
-static std::wofstream OpenLogFile()
-{
-    auto now = std::chrono::system_clock::now();
-    std::time_t t = std::chrono::system_clock::to_time_t(now);
-    std::tm tm{};
-    localtime_s(&tm, &t);
-    std::wstringstream name;
-    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";
-    std::wofstream f(LogsDir() / name.str(), std::ios::out | std::ios::trunc);
-    return f;
-}
+// Simple file logger under %LOCALAPPDATA%\ColonyGame\logs (UTF‑16LE with BOM)
+static fs::path LogsDir()
+{
+    fs::path out = winpath::writable_data_dir() / L"logs";
+    std::error_code ec;
+    fs::create_directories(out, ec);
+    return out;
+}
+
+static std::wofstream OpenLogFile()
+{
+    auto now = std::chrono::system_clock::now();
+    std::time_t t = std::chrono::system_clock::to_time_t(now);
+    std::tm tm{};
+    localtime_s(&tm, &t);
+    std::wstringstream name;
+    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";
+    // Open in binary so we can explicitly write a UTF‑16 BOM for better editor compatibility.
+    std::wofstream f(LogsDir() / name.str(), std::ios::out | std::ios::trunc | std::ios::binary);
+    if (f)
+    {
+        const wchar_t bom = 0xFEFF; // UTF‑16LE BOM
+        f.write(&bom, 1);
+        f.flush();
+    }
+    return f;
+}
@@
 #ifdef _DEBUG
 // NEW: Prefer attaching to an existing parent console (if launched from a terminal).
 static void AttachParentConsoleOrAlloc()
 {
     if (!AttachConsole(ATTACH_PARENT_PROCESS))
     {
         AllocConsole();
     }
     FILE* f = nullptr;
     freopen_s(&f, "CONOUT$", "w", stdout);
     freopen_s(&f, "CONOUT$", "w", stderr);
     freopen_s(&f, "CONIN$",  "r", stdin);
     SetConsoleOutputCP(CP_UTF8);
 }
 #endif
 
 // ---------- Entry point ----------
 int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
 {
     // NEW: Initialize crash dumps as early as possible (Saved Games\Colony Game\Crashes).
     wincrash::InitCrashHandler(L"Colony Game");
 
     // NEW: Enable fail-fast behavior on heap corruption as early as possible.
     EnableHeapTerminationOnCorruption();
 
     // Must run before any library loads to constrain DLL search order.
     EnableSafeDllSearch();
 
     // Ensure asset-relative paths work from any launch context (Explorer, VS, cmd).
     winpath::ensure_cwd_exe_dir();
 
     // Set error mode early to avoid OS popups for missing DLLs, etc.
     SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
 
     // NEW: Make message boxes crisp under high DPI scaling.
     EnableHighDpiAwareness();
@@
-    SingleInstanceGuard guard;
-    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF"))
+    SingleInstanceGuard guard;
+    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF"))
     {
         MsgBox(L"Colony Game", L"Another instance is already running.");
         return 0;
     }
@@
-    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE; // child does not inherit our error mode
+    // Note: CREATE_UNICODE_ENVIRONMENT is relevant only when lpEnvironment != nullptr; harmless otherwise.
+    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE; // child does not inherit our error mode
@@
-    if (!ok)
+    if (!ok)
     {
         DWORD err = GetLastError();
         log << L"[Launcher] CreateProcessW failed (" << err << L"): " << LastErrorMessage(err) << L"\n";
 #if defined(COLONY_EMBED_GAME_LOOP)
         log << L"[Launcher] Falling back to embedded safe mode.\n";
         return RunEmbeddedGameLoop(log);
 #else
         std::wstring msg = L"Failed to start game process.\n\nError " + std::to_wstring(err) + L": " + LastErrorMessage(err);
         MsgBox(L"Colony Game", msg);
         return 3;
 #endif
     }
@@
-    return static_cast<int>(code);
+    return static_cast<int>(code);
 }
