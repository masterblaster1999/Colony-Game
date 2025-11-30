#include "RawInput.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace wininput {
bool InitializeRawMouse(void* hwnd) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic desktop controls
    rid.usUsage     = 0x02; // Mouse
    rid.dwFlags     = RIDEV_INPUTSINK; // receive even when not focused
    rid.hwndTarget  = static_cast<HWND>(hwnd);
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

bool HandleRawInputMessage(void* lparam, RawMouseDelta& out) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) return false;
    BYTE* buffer = new BYTE[size];
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) != size) { delete[] buffer; return false; }

    RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buffer);
    if (ri->header.dwType == RIM_TYPEMOUSE) {
        const auto& m = ri->data.mouse;
        if (m.usFlags == MOUSE_MOVE_RELATIVE) {
            out.dx += static_cast<long>(m.lLastX);
            out.dy += static_cast<long>(m.lLastY);
        }
        if (m.usButtonFlags & RI_MOUSE_WHEEL) {
            out.wheel = true; out.wheelDelta += static_cast<short>(m.usButtonData);
        }
    }
    delete[] buffer;
    return true;
}
}
