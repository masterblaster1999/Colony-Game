diff --git a/platform/win/CrashDumpWin.cpp b/platform/win/CrashDumpWin.cpp
new file mode 100644
index 0000000..b3f43ad
--- /dev/null
+++ b/platform/win/CrashDumpWin.cpp
@@ -0,0 +1,197 @@
+#define WIN32_LEAN_AND_MEAN
+#include <Windows.h>
+#include <DbgHelp.h>          // MiniDumpWriteDump
+#include <filesystem>
+#include <string>
+#include <sstream>
+#include <iomanip>
+#include <system_error>
+#include "CrashDumpWin.h"
+
+#pragma comment(lib, "Dbghelp.lib")
+
+namespace fs = std::filesystem;
+
+namespace crash {
+namespace {
+
+std::wstring g_dumpDir;
+std::wstring g_appName;
+std::wstring g_appVer;
+
+static std::wstring NowStampCompact() {
+    SYSTEMTIME st; GetLocalTime(&st);
+    wchar_t buf[32]{};
+    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u",
+             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
+    return buf;
+}
+
+static fs::path EnsureDir(const fs::path& p) {
+    std::error_code ec;
+    fs::create_directories(p, ec);
+    return p;
+}
+
+static bool WriteDumpInternal(const fs::path& file, EXCEPTION_POINTERS* info) {
+    HANDLE hFile = CreateFileW(file.c_str(),
+                               GENERIC_WRITE,
+                               FILE_SHARE_READ,
+                               nullptr,
+                               CREATE_ALWAYS,
+                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
+                               nullptr);
+    if (hFile == INVALID_HANDLE_VALUE)
+        return false;
+
+    MINIDUMP_EXCEPTION_INFORMATION mei{};
+    mei.ThreadId = GetCurrentThreadId();
+    mei.ExceptionPointers = info;
+    mei.ClientPointers = FALSE;
+
+    // A rich-but-reasonable minidump: module list + thread info +
+    // indirectly referenced memory + data segments + unloaded modules.
+    MINIDUMP_TYPE type =
+        (MINIDUMP_TYPE)(MiniDumpWithThreadInfo
+                      | MiniDumpWithUnloadedModules
+                      | MiniDumpWithIndirectlyReferencedMemory
+                      | MiniDumpWithDataSegs);
+
+    BOOL ok = MiniDumpWriteDump(
+        GetCurrentProcess(),
+        GetCurrentProcessId(),
+        hFile,
+        type,
+        info ? &mei : nullptr,
+        nullptr,
+        nullptr);
+
+    FlushFileBuffers(hFile);
+    CloseHandle(hFile);
+    return ok == TRUE;
+}
+
+static LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* info) {
+    // Defensive: never crash here; avoid UI to keep shutdown clean.
+    __try {
+        const fs::path dir = EnsureDir(g_dumpDir);
+        std::wstringstream name;
+        name << (g_appName.empty() ? L"ColonyGame" : g_appName)
+             << L"-" << NowStampCompact()
+             << L"-" << GetCurrentProcessId()
+             << (g_appVer.empty() ? L"" : (L"-" + g_appVer))
+             << L".dmp";
+        const fs::path file = dir / name.str();
+        WriteDumpInternal(file, info);
+    } __except(EXCEPTION_EXECUTE_HANDLER) {
+        // swallow
+    }
+    // Tell the OS we've handled it; process will terminate normally.
+    return EXCEPTION_EXECUTE_HANDLER;
+}
+
+} // namespace
+
+void InstallCrashDumpHandler(const std::wstring& dumpDir,
+                             const wchar_t* appName,
+                             const wchar_t* appVersion) {
+    g_dumpDir = dumpDir;
+    g_appName = appName ? appName : L"ColonyGame";
+    g_appVer  = appVersion ? appVersion : L"";
+    EnsureDir(g_dumpDir);
+
+    // Avoid OS "stopped working" dialogs in production paths.
+    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
+    SetUnhandledExceptionFilter(&UnhandledExceptionFilterImpl);
+}
+
+bool WriteProcessMiniDump(const std::wstring& dumpDir,
+                          const wchar_t* appName,
+                          const wchar_t* appVersion,
+                          EXCEPTION_POINTERS* info) {
+    const fs::path dir = EnsureDir(dumpDir);
+    std::wstringstream name;
+    name << (appName && *appName ? appName : L"ColonyGame")
+         << L"-manual-" << NowStampCompact()
+         << L"-" << GetCurrentProcessId()
+         << (appVersion && *appVersion ? (std::wstring(L"-") + appVersion) : L"")
+         << L".dmp";
+    return WriteDumpInternal(dir / name.str(), info);
+}
+
+} // namespace crash
+
