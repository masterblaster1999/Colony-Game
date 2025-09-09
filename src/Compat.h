#pragma once

// This header is force-included by CMake for every translation unit.
// It prevents Windows headers from defining 'min'/'max' macros that break
// expressions like std::numeric_limits<T>::max() on MSVC.

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX 1
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
  #ifdef max
    #undef max
  #endif
  #ifdef min
    #undef min
  #endif
#endif

#include <cstdint>
#include <limits>

// Tip: If you ever *still* hit a macro collision from some third-party header,
// call the parenthesized form to block macro expansion, e.g.:
//   auto m = (std::numeric_limits<std::streamsize>::max)();
