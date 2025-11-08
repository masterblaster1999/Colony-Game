#pragma once

// First try the vcpkg-style include path, then the flat path.
// If neither exists, provide harmless no-op stubs so builds always succeed.
#if defined(__has_include)
  #if __has_include(<tracy/Tracy.hpp>)
    #include <tracy/Tracy.hpp>
  #elif __has_include(<Tracy.hpp>)
    #include <Tracy.hpp>
  #else
    // ---- Tracy stubs (no-op) ----
    #ifndef TRACY_ENABLE
      #define ZoneScoped
      #define ZoneScopedN(name)
      #define ZoneScopedC(color)
      #define ZoneNamed(var, active)
      #define ZoneNamedN(var, name, active)
      #define FrameMark
      #define FrameMarkStart(name)
      #define FrameMarkEnd(name)
      #define TracyPlot(name, value)
      #define TracyMessage(msg, size)
      #define TracyMessageL(msg)
      #define TracyAppInfo(txt, size)
    #endif
  #endif
#else
  // Very old compilers: fall back to stubs
  #ifndef TRACY_ENABLE
    #define ZoneScoped
    #define ZoneScopedN(name)
    #define ZoneScopedC(color)
    #define ZoneNamed(var, active)
    #define ZoneNamedN(var, name, active)
    #define FrameMark
    #define FrameMarkStart(name)
    #define FrameMarkEnd(name)
    #define TracyPlot(name, value)
    #define TracyMessage(msg, size)
    #define TracyMessageL(msg)
    #define TracyAppInfo(txt, size)
  #endif
#endif
