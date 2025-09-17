#pragma once
//
// DEPRECATED STUB: src/launcher/winerror.h
// This file exists only to keep old includes working temporarily.
// Please include "src/launcher/Win32ErrorUtil.hpp" directly.
//
// To silence the pragma message without updating call sites yet,
// define COLONY_SUPPRESS_WINERROR_STUB_WARNING before including this header.
//

#ifndef _WIN32
#error "src/launcher/winerror.h is Windows-only."
#endif

#if defined(_MSC_VER) && !defined(COLONY_SUPPRESS_WINERROR_STUB_WARNING)
#pragma message(__FILE__ " is deprecated; include \"src/launcher/Win32ErrorUtil.hpp\" instead.")
#endif

// Forward to the new, non-colliding header.
#include "Win32ErrorUtil.hpp"

// Nothing else is defined here on purpose.
// Keeping this stub minimal avoids reintroducing name collisions with the Windows SDK.
