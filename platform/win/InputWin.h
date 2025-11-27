// platform/win/InputWin.h
#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <functional>

namespace winplat {

// Lightweight sinks (function slots) for decoded raw input.
struct RawInputSinks {
  // Mouse motion: dx/dy; isAbsolute=true if device reports absolute coords.
  std::function<void(LONG dx, LONG dy, bool isAbsolute)> onMouseDelta;
  // Mouse wheel: wheel delta (positive/negative); horizontal==true for HWHEEL.
  std::function<void(short wheelDelta, bool horizontal)> onMouseWheel;
  // Keyboard: Win32 virtual key (possibly L/R specific); down=true on press.
  std::function<void(unsigned short vkey, bool down)> onKey;
};

// Registers mouse + keyboard for raw input (foreground by default).
// If sink==true, you will receive WM_INPUT even when the window is not focused.
bool RegisterRawMouseAndKeyboard(HWND hwnd, bool sink = false);

// Decodes a WM_INPUT message (lParam -> HRAWINPUT) and fans out to sinks.
void HandleRawInput(LPARAM lParam, const RawInputSinks& sinks);

} // namespace winplat
