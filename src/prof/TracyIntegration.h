#pragma once
//
// Tracy integration helpers for Colony-Game (Windows build).
//
// Key points:
//  * Don't pass runtime strings to ZoneScopedN(...) – it expects a string literal.
//    For dynamic names, open a zone with ZoneScoped and attach text via ZoneName().
//  * The Tracy macro 'FrameMark' marks the end of a frame; this header exposes a
//    wrapper named MarkFrame() to avoid macro name collisions.
//
// References:
//   - Lagrange docs: include <Tracy.hpp>, add FrameMark/ZoneScoped at call sites.
//   - Tracy docs/community: ZoneScopedN’s name is a string literal; use ZoneName() for runtime text.
//   - Manual notes about string lifetime and transient/dynamic naming.
//
#include <cstddef>

// ---------- Optional convenience macros ----------
// These expand to Tracy zones when TRACY_ENABLE is defined, otherwise to no-ops.
// They are safe to use anywhere in the codebase without additional #ifdefs.
#if defined(TRACY_ENABLE)
  #include <tracy/Tracy.hpp>
  #include <cstring>

  // Unnamed scope
  #define CG_PROF_SCOPE() \
      ZoneScoped

  // Named with a STRING LITERAL (fastest path; used for grouping)
  #define CG_PROF_SCOPE_N(literal) \
      ZoneScopedN(literal)

  // Dynamically named (runtime C-string). Uses ZoneName() after opening the zone.
  // NOTE: dynamic names are visible in UI but are not used for compile-time grouping.
  #define CG_PROF_SCOPE_DYNAMIC(runtimeNameCStr)                                      \
      do {                                                                            \
        ZoneScoped;                                                                   \
        if (const char* __cg_name__ = (runtimeNameCStr))                              \
          ZoneName(__cg_name__, static_cast<uint32_t>(std::strlen(__cg_name__)));     \
      } while (0)

#else
  // No-op fallbacks when Tracy is disabled
  #define CG_PROF_SCOPE()                      do{}while(0)
  #define CG_PROF_SCOPE_N(literal)             do{}while(0)
  #define CG_PROF_SCOPE_DYNAMIC(nameCStr)      do{}while(0)
#endif

// ---------- Minimal, engine-facing API ----------
// Tracy itself becomes a no-op when TRACY_ENABLE is not defined.
namespace prof {

// Mark the end of a frame (call once per frame).
// Intentionally not named 'FrameMark' to avoid colliding with Tracy macro.
void MarkFrame();

// Optional: push a transient message into the capture (visible in timeline).
void Message(const char* text);

// Example scope helper (RAII) – safe for dynamic names.
// Opens a zone; if a name is provided, attaches it at runtime via ZoneName().
struct Scope {
  explicit Scope(const char* name = nullptr);
  ~Scope();
};

} // namespace prof
