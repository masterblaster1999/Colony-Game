#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <ShlObj.h>     // SHGetKnownFolderPath
#include <Shlwapi.h>
#include <shellapi.h>
#include <combaseapi.h> // CoTaskMemFree
#include <DbgHelp.h>    // Crash dumps
