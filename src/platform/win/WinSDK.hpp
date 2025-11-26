#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
// Explicit “opt-in” headers that LEAN_AND_MEAN would otherwise omit:
#include <shellapi.h>   // CommandLineToArgvW, DragQueryFile, etc.
#include <shlobj_core.h> // if you use SHGetKnownFolderPath elsewhere
