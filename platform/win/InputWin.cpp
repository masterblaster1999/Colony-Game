// platform/win/InputWin.cpp
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include "InputWin.h"

namespace winplat {

bool RegisterRawMouseAndKeyboard(HWND hwnd, bool sink)
{
  RAWINPUTDEVICE rid[2] = {};
  // Mouse: Usage Page 0x01, Usage 0x02
  rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
  rid[0].dwFlags     = sink ? RIDEV_INPUTSINK : 0; // foreground by default
  rid[0].hwndTarget  = hwnd;

  // Keyboard: Usage Page 0x01, Usage 0x06
  rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;
  rid[1].dwFlags     = sink ? RIDEV_INPUTSINK : 0;
  rid[1].hwndTarget  = hwnd;

  return ::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) == TRUE;
}

static inline unsigned short MapLeftRightVK(unsigned short vkey, unsigned short make, USHORT flags)
{
  // Map generic VKs to left/right variants when possible.
  const bool e0 = (flags & RI_KEY_E0) != 0;
  switch (vkey) {
    case VK_SHIFT:
      // MapVirtualKeyW(VSC->VK_EX) can distinguish L/R Shift.
      return static_cast<unsigned short>(::MapVirtualKeyW(make, MAPVK_VSC_TO_VK_EX));
    case VK_CONTROL:
      return e0 ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU: // ALT
      return e0 ? VK_RMENU : VK_LMENU;
    default:
      return vkey;
  }
}

void HandleRawInput(LPARAM lParam, const RawInputSinks& sinks)
{
  HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(lParam);

  UINT size = 0;
  if (::GetRawInputData(hRaw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 || size == 0)
    return;

  std::vector<BYTE> buffer(size);
  if (::GetRawInputData(hRaw, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
    return;

  RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
  if (raw->header.dwType == RIM_TYPEMOUSE) {
    const RAWMOUSE& m = raw->data.mouse;

    // Motion
    if (sinks.onMouseDelta) {
      const bool absolute = (m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
      sinks.onMouseDelta(m.lLastX, m.lLastY, absolute);
    }

    // Wheels (vertical/horizontal)
    if (sinks.onMouseWheel) {
      if (m.usButtonFlags & RI_MOUSE_WHEEL) {
        const short delta = static_cast<short>(m.usButtonData); // WHEEL_DELTA multiples
        sinks.onMouseWheel(delta, /*horizontal*/false);
      }
      if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
        const short delta = static_cast<short>(m.usButtonData);
        sinks.onMouseWheel(delta, /*horizontal*/true);
      }
    }
  }
  else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
    const RAWKEYBOARD& k = raw->data.keyboard;
    const bool keyUp   = (k.Flags & RI_KEY_BREAK) != 0;
    const bool keyDown = !keyUp;
    unsigned short vkey = MapLeftRightVK(k.VKey, k.MakeCode, k.Flags);

    if (sinks.onKey) sinks.onKey(vkey, keyDown);
  }
  // (Other HID devices: raw->header.dwType == RIM_TYPEHID)
}

} // namespace winplat
