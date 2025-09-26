diff --git a/platform/win/CrashHandler.cpp b/platform/win/CrashHandler.cpp
new file mode 100644
--- /dev/null
+++ b/platform/win/CrashHandler.cpp
@@ -0,0 +1,66 @@
+#include "CrashHandler.h"
+#include <windows.h>
+#include <dbghelp.h>
+#include <shlobj.h>   // SHGetKnownFolderPath
+#include <string>
+
+static std::wstring CG_GetCrashDir() {
+    PWSTR path = nullptr;
+    std::wstring out;
+    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
+        out.assign(path);
+        CoTaskMemFree(path);
+        out += L"\\ColonyGame\\Crashes";
+        CreateDirectoryW(out.c_str(), nullptr);
+    } else {
+        out = L".";
+    }
+    return out;
+}
+
+static LONG WINAPI CG_Unhandled(EXCEPTION_POINTERS* ep) {
+    SYSTEMTIME st; GetLocalTime(&st);
+    std::wstring dir = CG_GetCrashDir();
+    wchar_t file[MAX_PATH];
+    swprintf_s(file, L"%s\\ColonyGame_%04u%02u%02u_%02u%02u%02u.dmp",
+               dir.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
+
+    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
+    if (h != INVALID_HANDLE_VALUE) {
+        MINIDUMP_EXCEPTION_INFORMATION mei{};
+        mei.ThreadId = GetCurrentThreadId();
+        mei.ExceptionPointers = ep;
+        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpNormal, &mei, nullptr, nullptr);
+        CloseHandle(h);
+    }
+    return EXCEPTION_EXECUTE_HANDLER;
+}
+
+void InstallCrashHandler(const wchar_t*) {
+    SetUnhandledExceptionFilter(CG_Unhandled);
+}
