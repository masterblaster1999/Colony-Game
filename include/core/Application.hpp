// include/core/Application.hpp
#pragma once

#if !defined(_WIN32)
  #error "Colony-Game targets Windows only."
#endif

// Keep Windows headers lean + avoid min/max macro pollution.
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <Windows.h>

// Main entry called from WinLauncher.cpp (and implemented in core/Application.cpp).
[[nodiscard]] int RunColonyGame(HINSTANCE hInstance);
