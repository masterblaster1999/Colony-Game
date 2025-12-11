// tests/doctest.h
// Compatibility wrapper so legacy #include "doctest.h" works in this repo
// while we keep the official upstream layout <doctest/doctest.h>.
//
// IMPORTANT:
// - Do NOT define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here.
//   Define it in exactly one translation unit (e.g., test_main.cpp).
// - This wrapper only redirects the include to the official header.
//
// MSVC note: __has_include is supported in MSVC since VS 2017 15.3.
// See Microsoft conformance table. If you're on an older toolset,
// fix the include paths instead of editing this file.

#pragma once

#if defined(__has_include)
  // Try the canonical installed / fetched path first
  #if __has_include(<doctest/doctest.h>)
    #include <doctest/doctest.h>

  // Common vendored layouts
  #elif __has_include("doctest/doctest.h")
    #include "doctest/doctest.h"
  #elif __has_include(<external/doctest/doctest.h>)
    #include <external/doctest/doctest.h>
  #elif __has_include("external/doctest/doctest.h")
    #include "external/doctest/doctest.h"

  // Nothing matched → give a helpful error with hints
  #else
    #error \
"doctest header not found. Expected <doctest/doctest.h> (or equivalent) to be on the include path. \
If you use FetchContent, add '${doctest_upstream_SOURCE_DIR}' to target_include_directories(); \
otherwise install doctest and add its include directory. \
See https://github.com/doctest/doctest (tutorial: #include \"doctest.h\" works only if the header \
is co-located or your include path points to it)."
  #endif

#else
  // Fallback for very old compilers: hope the canonical path is available.
  #include <doctest/doctest.h>
#endif
