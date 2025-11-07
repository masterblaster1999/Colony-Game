#pragma once

// Centralized Tracy integration for Windows builds.
// Works even if TRACY_ENABLE (or COLONY_WITH_TRACY) is undefined: all helpers become no-ops.

#include <cstddef>
#include <cstdint>

namespace prof {

// Initialize / shutdown (no-ops for Tracy client; kept for symmetry/future)
void Init(const char* appName = nullptr);
void Shutdown();

// Mark a frame boundary (appears as vertical frame ticks in Tracy)
void MarkFrame();
void MarkFrameNamed(const char* name);
void MarkFrameStart(const char* name);
void MarkFrameEnd(const char* name);

// Name threads in Tracy UI (show up on the left-side thread list)
void SetThreadName(const char* name);

// Emit an arbitrary message into the timeline (optional helper)
void Message(const char* text, std::size_t len);
void MessageColor(const char* text, std::size_t len, std::uint32_t rgb);

// RAII zone macro for scopes. Example:
//   void Update() { PROF_SCOPE("Update"); ... }
} // namespace prof

// --- Instrumentation toggle & header probing ---
// Define COLONY_WITH_TRACY (or TRACY_ENABLE) in your build to enable instrumentation.
#if defined(COLONY_WITH_TRACY) || defined(TRACY_ENABLE)
  #ifndef TRACY_ENABLE
    #define TRACY_ENABLE 1
  #endif

  // Prefer the canonical vcpkg / upstream include path: <tracy/Tracy.hpp>.
  // Fall back to "Tracy.hpp" if the project vendors Tracy directly.
  #if defined(__has_include)
    #if __has_include(<tracy/Tracy.hpp>)
      #include <tracy/Tracy.hpp>
    #elif __has_include("Tracy.hpp")
      #include "Tracy.hpp"
    #elif __has_include(<Tracy.hpp>)
      #include <Tracy.hpp>
    #else
      // Headers not found → gracefully disable Tracy instead of failing CI.
      #undef TRACY_ENABLE
    #endif
  #else
    // If the compiler doesn't support __has_include, try canonical layout;
    // if it fails, TRACY_ENABLE will effectively be ignored by stubs below.
    #include <tracy/Tracy.hpp>
  #endif

  #if defined(TRACY_ENABLE)
    #include <cstring>

    // Scoped profiling zone with optional runtime name.
    #define PROF_SCOPE(nameLiteralOrPtr)                                      \
      ZoneScoped;                                                             \
      do {                                                                    \
        const char* _nm = (nameLiteralOrPtr);                                 \
        if (_nm) ZoneName(_nm, (uint32_t)std::strlen(_nm));                   \
      } while (0)
  #else
    // Tracy was requested but headers were not found → provide harmless stubs.
    #define PROF_SCOPE(nameLiteralOrPtr) do { (void)sizeof(nameLiteralOrPtr); } while (0)
    #ifndef FrameMark
      #define FrameMark do {} while (0)
    #endif
    #ifndef FrameMarkStart
      #define FrameMarkStart(name) do { (void)sizeof(name); } while (0)
    #endif
    #ifndef FrameMarkEnd
      #define FrameMarkEnd(name) do { (void)sizeof(name); } while (0)
    #endif
  #endif

#else
  // Compile away profiling when not requested.
  #define PROF_SCOPE(nameLiteralOrPtr) do { (void)sizeof(nameLiteralOrPtr); } while (0)

  // If code elsewhere uses Tracy's frame macros directly, provide harmless stubs.
  #ifndef FrameMark
    #define FrameMark do {} while (0)
  #endif
  #ifndef FrameMarkStart
    #define FrameMarkStart(name) do { (void)sizeof(name); } while (0)
  #endif
  #ifndef FrameMarkEnd
    #define FrameMarkEnd(name) do { (void)sizeof(name); } while (0)
  #endif
#endif
