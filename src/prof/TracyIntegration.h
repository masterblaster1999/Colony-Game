#pragma once

// Minimal, engine-facing API. Tracy is header-only from your POV.
// Tracy itself becomes a no-op when TRACY_ENABLE is not defined.
namespace prof {

// Mark the end of a frame (call once per frame).
void FrameMark();

// Optional: push a transient message into the capture (visible in timeline).
void Message(const char* text);

// Example scope helper (RAII) if you prefer explicit scopes over macros:
struct Scope {
  Scope(const char* name);
  ~Scope();
};

} // namespace prof
