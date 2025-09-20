diff --git a/platform/win/CrashDumpWin.h b/platform/win/CrashDumpWin.h
new file mode 100644
index 0000000..e1a2c7b
--- /dev/null
+++ b/platform/win/CrashDumpWin.h
@@ -0,0 +1,34 @@
+#pragma once
+// Windows-only crash dump writer for unhandled exceptions.
+// Usage:
+//   crash::InstallCrashDumpHandler(dumpDir, L"ColonyGame", L"v1");
+//
+// Writes dumps like: <dumpDir>\ColonyGame-YYYYMMDD-HHMMSS-PID.dmp
+// See: MiniDumpWriteDump (DbgHelp) and SetUnhandledExceptionFilter.
+// Docs: https://learn.microsoft.com/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump
+
+#ifndef WIN32_LEAN_AND_MEAN
+#define WIN32_LEAN_AND_MEAN
+#endif
+#include <string>
+
+namespace crash {
+
+// Installs a top-level unhandled-exception filter that writes a .dmp in dumpDir.
+// dumpDir is created if missing. appName/version only affect the file name.
+// Thread-safe to call once during process startup.
+void InstallCrashDumpHandler(const std::wstring& dumpDir,
+                             const wchar_t* appName,
+                             const wchar_t* appVersion = L"");
+
+// Optional: explicitly force a dump at a safe point (e.g., from a bug-report menu).
+// If 'info' is null, writes a non-exception dump (no fault context).
+// Returns true on success.
+bool WriteProcessMiniDump(const std::wstring& dumpDir,
+                          const wchar_t* appName,
+                          const wchar_t* appVersion = L"",
+                          struct _EXCEPTION_POINTERS* info = nullptr);
+
+} // namespace crash
+
