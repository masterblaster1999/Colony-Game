// platform/win/CrashHandler.hpp
#pragma once

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string_view>

namespace cg::win {

enum class DumpKind {
    Minidump,      // small, usually enough to analyze call stacks
    FullMemory     // larger dump incl. memory (use sparingly)
};

// Install once at process start; safe to call multiple times.
void InstallCrashHandler(std::wstring_view appName = L"ColonyGame",
                         DumpKind kind = DumpKind::Minidump);

// Optional: remove our handler and restore the previous one.
void UninstallCrashHandler();

// Optional: attach a short note (e.g., current scene/state). Thread‑safe.
void SetCrashExtraNote(std::wstring_view note);

// Returns directory where dumps are written (e.g., %LOCALAPPDATA%\ColonyGame\crashdumps).
// Guaranteed to exist after InstallCrashHandler().
const wchar_t* GetCrashDumpDirectory();

} // namespace cg::win
