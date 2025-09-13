diff --git a/src/launcher/CrashDump.h b/src/launcher/CrashDump.h
index 1111111..2222222 100644
--- a/src/launcher/CrashDump.h
+++ b/src/launcher/CrashDump.h
@@ -1,14 +1,171 @@
-#pragma once
-#define WIN32_LEAN_AND_MEAN
-#include <windows.h>
-
-// Install top-level unhandled exception filter for the *current* process.
-void InstallCrashHandler();
-
-// Helper: writes a minidump for the *current* process to the default crash folder.
-// You rarely need to call this directly; InstallCrashHandler() does it on crashes.
-bool WriteProcessMiniDump(EXCEPTION_POINTERS* ep);
-
-// Returns something like: %LOCALAPPDATA%\\ColonyGame\\crash
-// Folder is created if missing.
-const wchar_t* GetCrashDir();
+// Copyright (c) Colony Game
+// Windows-only crash dump helper — upgraded interface
+// This header preserves the original API (deprecated) and adds a modern,
+// configurable interface under cg::crash.
+#pragma once
+#ifndef CG_CRASHDUMP_H_
+#define CG_CRASHDUMP_H_
+
+#if !defined(_WIN32)
+#  error "CrashDump.h is Windows-only."
+#endif
+
+#ifndef WIN32_LEAN_AND_MEAN
+#  define WIN32_LEAN_AND_MEAN
+#endif
+#include <windows.h>     // EXCEPTION_POINTERS
+#include <dbghelp.h>     // MINIDUMP_TYPE
+#include <cstddef>
+#include <cstdint>
+#include <functional>
+#include <string>
+#include <string_view>
+
+// ==========================
+// New API (namespace cg::crash)
+// ==========================
+namespace cg::crash {
+
+/**
+ * @brief Configuration for the crash handler / minidump writer.
+ *
+ * All fields are optional. Reasonable defaults are used when empty:
+ * - dumpDirectory defaults to "%LOCALAPPDATA%\\ColonyGame\\crash" (created if missing).
+ * - appName defaults to the executable base name.
+ * - dumpType defaults to a balanced set appropriate for post-mortem debugging.
+ * - retention keeps the N most recent .dmp files (older are deleted).
+ * - writeOn* flags control which failure paths auto-write a dump.
+ */
+struct Config {
+    std::wstring appName;              ///< e.g. L"ColonyGame"
+    std::wstring appVersion;           ///< e.g. L"1.0.0"; appended to filename if set
+    std::wstring dumpDirectory;        ///< if empty, a default per-user path is used
+    MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
+        MiniDumpWithIndirectlyReferencedMemory |
+        MiniDumpScanMemory |
+        MiniDumpWithThreadInfo |
+        MiniDumpWithFullMemoryInfo |
+        MiniDumpWithHandleData |
+        MiniDumpWithUnloadedModules |
+        MiniDumpWithProcessThreadData
+    );
+    std::size_t  retention = 20;       ///< keep most-recent N dumps (0 = unlimited)
+
+    // Auto-write behaviors:
+    bool writeOnUnexpectedSEH    = true;  ///< top-level unhandled SEH
+    bool writeOnTerminate        = true;  ///< std::terminate
+    bool writeOnPureCall         = true;  ///< MSVC pure virtual call
+    bool writeOnInvalidParameter = true;  ///< invalid CRT parameter
+
+    // Filename and side-car options:
+    bool appendPid        = true;   ///< include process id in filename
+    bool appendTimestamp  = true;   ///< include local timestamp in filename
+    bool createSidecarTxt = true;   ///< write a small .txt next to the .dmp
+
+    // Optional user-provided comment embedded in the dump (Unicode).
+    std::wstring comment;
+
+    // Optional filter: return false to skip writing the dump.
+    std::function<bool(_In_opt_ EXCEPTION_POINTERS* ep)> filter;
+
+    // Optional callback invoked after a dump is successfully written.
+    std::function<void(const std::wstring& dumpPath)> onDumpWritten;
+};
+
+/**
+ * @brief RAII installer for the crash handler.
+ * Constructs => installs with the provided config.
+ * Destructs  => uninstalls and restores previous handlers.
+ */
+class ScopedInstall {
+public:
+    explicit ScopedInstall(const Config& cfg) noexcept;
+    ~ScopedInstall() noexcept;
+    ScopedInstall(ScopedInstall&&) noexcept;
+    ScopedInstall& operator=(ScopedInstall&&) noexcept;
+    ScopedInstall(const ScopedInstall&)            = delete;
+    ScopedInstall& operator=(const ScopedInstall&) = delete;
+    [[nodiscard]] bool ok() const noexcept { return installed_; }
+private:
+    bool installed_{false};
+};
+
+/**
+ * @brief Globally install the crash handler with configuration.
+ * Safe to call multiple times; subsequent calls update configuration.
+ * @return true if installed or updated successfully.
+ */
+[[nodiscard]] bool Install(const Config& cfg) noexcept;
+
+/// @brief Uninstall and restore previous process handlers.
+void Uninstall() noexcept;
+
+/// @brief Returns whether the handler is currently installed.
+[[nodiscard]] bool IsInstalled() noexcept;
+
+/**
+ * @brief Write a minidump for the current process.
+ * @param ep      Optional exception pointers (pass nullptr for non-exception dumps).
+ * @param outPath Optional: receives the absolute dump file path on success.
+ * @return true if a dump was written.
+ */
+[[nodiscard]] bool WriteMiniDump(_In_opt_ EXCEPTION_POINTERS* ep = nullptr,
+                                 _Out_opt_ std::wstring* outPath = nullptr) noexcept;
+
+/**
+ * @brief Returns the crash directory (ensures it exists). Uses configured or default path.
+ * @return Absolute directory path as a std::wstring.
+ */
+[[nodiscard]] std::wstring GetCrashDirectory();
+
+/// ----- Live metadata & breadcrumbs (embedded via a custom user stream) -----
+
+/// @brief Set/overwrite a metadata key => value pair (thread-safe).
+void SetMetadata(std::wstring key, std::wstring value);
+
+/// @brief Clear all metadata pairs.
+void ClearMetadata() noexcept;
+
+/// @brief Append a breadcrumb line (ring-buffered; included with dumps).
+void AddBreadcrumb(std::wstring_view line) noexcept;
+
+/// @brief Set ring-buffer capacity for breadcrumbs (default 64).
+void SetBreadcrumbCapacity(std::size_t capacity) noexcept;
+
+/// @brief Clear breadcrumb buffer.
+void ClearBreadcrumbs() noexcept;
+
+/// @brief Update only the dump type at runtime.
+void SetDumpType(MINIDUMP_TYPE type) noexcept;
+
+/// @brief Update only the crash directory at runtime (created if missing).
+void SetDumpDirectory(std::wstring absolutePath);
+
+/// @brief Install/replace the filter callback at runtime.
+void SetFilter(std::function<bool(_In_opt_ EXCEPTION_POINTERS*)> filter) noexcept;
+
+// Custom user stream id used to embed JSON metadata/breadcrumbs as UTF-8.
+inline constexpr std::uint32_t kUserStream_MetadataUTF8 = 0xC0111001u;
+
+} // namespace cg::crash
+
+
+// ==========================
+// Legacy API (preserved; deprecated)
+// ==========================
+// These remain declared for backward compatibility with existing code.
+// They are implemented in the corresponding .cpp and may forward to the new API.
+
+/// Install top-level unhandled exception filter for the *current* process.
+[[deprecated("Use cg::crash::Install with cg::crash::Config.")]]
+void InstallCrashHandler();
+
+/// Helper: writes a minidump for the *current* process to the default crash folder.
+/// Prefer cg::crash::WriteMiniDump.
+[[deprecated("Use cg::crash::WriteMiniDump.")]]
+bool WriteProcessMiniDump(_In_opt_ EXCEPTION_POINTERS* ep);
+
+/// Returns something like: %LOCALAPPDATA%\\ColonyGame\\crash (folder created if missing).
+[[deprecated("Use cg::crash::GetCrashDirectory().")]]
+const wchar_t* GetCrashDir();
+
+#endif // CG_CRASHDUMP_H_
