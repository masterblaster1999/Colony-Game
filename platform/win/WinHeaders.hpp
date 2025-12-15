#pragma once

// Centralized Windows headers for the project.
// Keep Windows macro pollution consistent across the codebase.

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#ifndef STRICT
  #define STRICT
#endif

// If you ever include winsock2.h elsewhere, this prevents winsock.h redefinition issues.
#ifndef _WINSOCKAPI_
  #define _WINSOCKAPI_
#endif

// Windows API
#include <windows.h>

// Common shell APIs you already use in other Win-only utilities.
#include <shellapi.h>
#include <ShlObj.h>
#include <KnownFolders.h>
