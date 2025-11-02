#pragma once
// platform/win/RuntimeSupport.h
//
// Windows-specific runtime helpers for Colony-Game.
// Patch: stop redefining WIN32_LEAN_AND_MEAN here; include the guarded umbrella instead.
// See also: Using the Windows Headers (define WIN32_LEAN_AND_MEAN once, before <Windows.h>).
//
// - FixWorkingDirectoryToExe(): call at the very top of WinMain to make relative paths stable.
//   (Uses GetModuleFileNameW and SetCurrentDirectory.) 
// - EnsureSingleInstance(): named-mutex guard; returns false if another instance already runs.
//   NOTE: For per-user single-instance or stronger guarantees, prefer a per-user lock file.
// - SetPerMonitorDpiAware(): best-effort runtime DPI awareness; Microsoft recommends manifest
//   Per-Monitor (V2) for production.
// - InitCrashHandler(): initialize your crash-dump pipeline (directory is optional).
//
// References:
//   * Using the Windows Headers — WIN32_LEAN_AND_MEAN. 
//   * CreateMutex/ERROR_ALREADY_EXISTS and single-instance cautions.
//   * SetProcessDpiAwarenessContext note: prefer manifest over API.
//   * GetModuleFileNameW / SetCurrentDirectoryW.
//   (See repo docs or implementation file for details.)

#include "platform/win/WinCommon.h"  // guarded WIN32_LEAN_AND_MEAN/NOMINMAX + <Windows.h>
#include <string>

// Call at the very top of WinMain (before any file I/O):
void FixWorkingDirectoryToExe();

// Returns false if already running (named mutex was pre-existing).
// Consider a per-user lock or randomized name if you need stronger guarantees.
[[nodiscard]] bool EnsureSingleInstance(_In_z_ const wchar_t* mutexName);

// Best-effort Per-Monitor DPI enabling at runtime.
// Microsoft recommends setting DPI awareness in the application manifest;
// this call exists as a fallback when a manifest is not present or cannot be changed.
void SetPerMonitorDpiAware();

// Initialize crash-dump subsystem.
// 'dumpSubdir' is a relative folder name (e.g., L"crashdumps") under a writable base.
// Returns true on success.
[[nodiscard]] bool InitCrashHandler(_In_opt_z_ const wchar_t* dumpSubdir = L"crashdumps");

// Utilities (optional)
[[nodiscard]] std::wstring GetExeDir();
[[nodiscard]] bool DirectoryExists(const std::wstring& path);
