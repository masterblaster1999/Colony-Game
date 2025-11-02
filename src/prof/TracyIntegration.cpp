#include "TracyIntegration.h"

// IMPORTANT: Starting with Tracy v0.9, you must add tracy/public to include dirs
// and include with <tracy/Tracy.hpp>. See Tracy NEWS and docs.
#include <tracy/Tracy.hpp>   // provides FrameMark, ZoneScoped, etc. (safe if TRACY_ENABLE is off)

// Optional: if you enable GPU capture later (D3D11)
// #include <tracy/TracyD3D11.hpp>

namespace prof {

void FrameMark() {
  // Marks the end of a frame in the capture.
  // Put this at the end of your game loop.
  FrameMark; // Semicolon intentionally present; Tracy requires it.
}

void Message(const char* text) {
  // Posts a message into the timeline.
  // Safe to call even if TRACY_ENABLE is undefined (becomes no-op).
  TracyMessage(text, std::strlen(text));
}

Scope::Scope(const char* name) {
  // Creates a named zone for the lifetime of this object.
  // Prefer explicit macro if you want zero overhead in release builds.
  ZoneScopedN(name);
}

Scope::~Scope() {
  // ZoneScopedN ends automatically on destruction.
}

} // namespace prof
