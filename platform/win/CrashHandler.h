diff --git a/platform/win/CrashHandler.h b/platform/win/CrashHandler.h
new file mode 100644
--- /dev/null
+++ b/platform/win/CrashHandler.h
@@ -0,0 +1,6 @@
+#pragma once
+
+// Installs an unhandled-exception filter that writes a minidump to
+// %LOCALAPPDATA%\ColonyGame\Crashes.
+// Call once near program start.
+void InstallCrashHandler(const wchar_t* dumpPrefix = L"ColonyGame");
