// platform/win/PathUtilWin.cpp
//
// Legacy stub TU.
//
// CMake's src/CMakeLists.txt currently globs all platform/win/*.cpp into
// the colony_core static library. The actual implementations of the
// winpath:: helpers now live in PathUtilWin.h as inline functions.
//
// Keeping this file (but empty of definitions) avoids linker errors without
// duplicating the implementations.

#include "PathUtilWin.h"

// Intentionally no non-inline definitions here.
// See platform/win/PathUtilWin.h for the implementation of:
//
//   std::filesystem::path winpath::exe_path();
//   std::filesystem::path winpath::exe_dir();
//   void                  winpath::ensure_cwd_exe_dir();
//   std::filesystem::path winpath::resource_dir();
//   std::filesystem::path winpath::writable_data_dir();
//   std::filesystem::path winpath::saved_games_dir(const wchar_t*);
//   bool                  winpath::atomic_write_file(...);
