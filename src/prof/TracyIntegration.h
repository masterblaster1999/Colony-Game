#pragma once

// Minimal facade to avoid sprinkling TRACY_ENABLE all over the codebase.
// Works even if TRACY_ENABLE is undefined (all functions become no-ops).

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

// Use a macro to create a scoped zone with an optional runtime name.
// (This never redeclares any Tracy internals.)
#if defined(TRACY_ENABLE)
  #include <Tracy.hpp>
  #include <cstring>
  #define PROF_SCOPE(nameLiteralOrPtr) \
      ZoneScoped; \
      do { const char* _nm = (nameLiteralOrPtr); if (_nm) ZoneName(_nm, (uint32_t)std::strlen(_nm)); } while(0)
#else
  #define PROF_SCOPE(nameLiteralOrPtr) do { (void)sizeof(nameLiteralOrPtr); } while(0)
#endif
