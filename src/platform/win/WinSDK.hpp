#pragma once

// Keep Windows headers tidy and predictable across the project.
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

// Highest available SDK features (optional, harmless if present)
#include <sdkddkver.h>

// Core Win32 + error/HRESULT macros (SUCCEEDED/FAILED, S_OK, E_*, ERROR_*)
#include <Windows.h>
#include <winerror.h>

// COM base helpers (CoInitializeEx, CoUninitialize, CoTaskMemFree, etc.)
#include <combaseapi.h>

// If you use shell COM interfaces (IFileDialog, IShellLink, etc.),
// include ShObjIdl AFTER Windows.h to ensure SUCCEEDED is defined.
// Do this in .cpp files that actually need shell types to avoid heavy headers.
