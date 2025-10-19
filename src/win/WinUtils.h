#pragma once
//
// WinUtils.h — Windows utilities
//
// INCLUDE-ORDER NOTE (MSVC / Windows only):
//   This header intentionally ensures <Windows.h> is included *after* the usual
//   configuration macros are set, to avoid common macro clashes and speed up builds.
//
//   - Define WIN32_LEAN_AND_MEAN before including <Windows.h> to trim unused APIs
//     and reduce compile time.
//   - Define NOMINMAX before including <Windows.h> so std::min/std::max are not
//     macro-expanded by the Windows headers.
//   - Per Microsoft guidance, configuration macros only take effect if they are
//     defined *before the very first inclusion* of <Windows.h> in the TU.
//     If some other header included <Windows.h> earlier, those settings have
//     already been fixed for this translation unit.
//
//   Actionable rule for this codebase:
//     ➤ Include **WinUtils.h** (or your central Win platform header) before any
//       third‑party or engine header that might include <Windows.h>.
//
//   If you need to track down who included <Windows.h> first, turn on
//   MSVC’s /showIncludes to see the include tree.
//
//   References:
//     - Using the Windows Headers (WIN32_LEAN_AND_MEAN) – Microsoft Learn
//     - DirectXMath note on NOMINMAX before Windows headers – Microsoft Learn
//     - Raymond Chen: “Configure a header *before* you include it” – Old New Thing
//

// If <Windows.h> was already included (without our guards), emit a gentle reminder.
// _WINDOWS_ is the traditional include guard used by <Windows.h>.
#if defined(_WINDOWS_) && !defined(NOMINMAX)
#  pragma message( \
   "Warning: <Windows.h> was included before NOMINMAX; std::min/max may clash. "\
   "Include WinUtils.h earlier or define NOMINMAX in project settings.")
#endif

// Configure Windows headers now (guarded to avoid C4005 if set on the command line).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <Windows.h>  // must follow the guards

#include <filesystem>
#include <string>

// Public API
std::filesystem::path GetExecutableDir();
std::wstring          GetLastErrorMessage(DWORD err);
void                  TryHardenDllSearch();
