// engine/win/input/RawInput.cpp
#include <cstdint>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>

#include "RawInput.h"

namespace wininput
{
bool InitializeRawMouse(void* hwnd)
{
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;      // Generic desktop controls
    rid.usUsage     = 0x02;      // Mouse
    rid.dwFlags     = RIDEV_INPUTSINK; // Receive WM_INPUT even when not focused
    rid.hwndTarget  = static_cast<HWND>(hwnd);

    return ::RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
}

bool HandleRawInputMessage(void* lparam, RawMouseDelta& out)
{
    if (!lparam)
        return false;

    const HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(lparam);

    UINT size = 0;
    constexpr UINT kHeaderSize = sizeof(RAWINPUTHEADER);

    // Query required size. Success returns 0 and sets `size`.
    const UINT q = ::GetRawInputData(hRaw, RID_INPUT, nullptr, &size, kHeaderSize);
    if (q != 0 || size == 0)
        return false;

    std::vector<std::uint8_t> buffer(size);

    // Read the input data.
    const UINT bytes = ::GetRawInputData(hRaw, RID_INPUT, buffer.data(), &size, kHeaderSize);
    if (bytes == static_cast<UINT>(-1) || bytes != size)
        return false;

    const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (!ri || ri->header.dwType != RIM_TYPEMOUSE)
        return false;

    const RAWMOUSE& m = ri->data.mouse;

    // Relative movement is the default unless MOUSE_MOVE_ABSOLUTE is set.
    if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
    {
        out.dx += static_cast<decltype(out.dx)>(m.lLastX);
        out.dy += static_cast<decltype(out.dy)>(m.lLastY);
    }

    if ((m.usButtonFlags & RI_MOUSE_WHEEL) != 0)
    {
        out.wheel = true;
        // Wheel delta is signed; usButtonData is USHORT.
        out.wheelDelta += static_cast<decltype(out.wheelDelta)>(static_cast<SHORT>(m.usButtonData));
    }

    return true;
}
} // namespace wininput
