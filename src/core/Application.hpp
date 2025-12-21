// core/Application.hpp
//
// Windows-only public interface for the Colony-Game application core.
// - Exposes a stable C ABI for the platform layer (Win32 message loop).
// - Keeps the C++ Application type opaque (defined in core/Application.cpp).
// - Safe to include from both C and C++ translation units.
// - No Linux/macOS codepaths.

#pragma once

// -----------------------------------------------------------------------------
// Windows-only guard
// -----------------------------------------------------------------------------
#ifndef _WIN32
#  error "core/Application.hpp is Windows-only. This project targets Win32."
#endif

// -----------------------------------------------------------------------------
// Forward declarations to avoid dragging in <windows.h> everywhere.
// -----------------------------------------------------------------------------
struct HWND__;
using HWND = HWND__*;

// -----------------------------------------------------------------------------
// Opaque C++ forward declaration of the Application type.
// Only the .cpp defines the class; other TUs should call the C ABI below.
// -----------------------------------------------------------------------------
#ifdef __cplusplus
namespace colony {
  class Application; // defined in core/Application.cpp
} // namespace colony
#endif

// -----------------------------------------------------------------------------
// Public C ABI used by the platform/win layer
// -----------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// Initialize the application with an existing HWND and initial client size.
// Returns true on success.
bool colony_app_init(HWND hwnd, int width, int height);

// Advance one frame (fixed-step simulation inside; one render call per tick).
void colony_app_tick();

// Notify the application that the client area has changed size.
void colony_app_resize(int width, int height);

// Shutdown and free all resources.
void colony_app_shutdown();

#ifdef __cplusplus
} // extern "C"
#endif
