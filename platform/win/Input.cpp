// platform/win/Input.cpp
#include "Input.h"
#include <hidusage.h>      // HID usage pages/usages for Raw Input
#include <cmath>

using namespace Colony::Win;

static DWORD WINAPI XInputGetStateStub(DWORD, XINPUT_STATE*) { return ERROR_DEVICE_NOT_CONNECTED; }
static DWORD WINAPI XInputSetStateStub(DWORD, XINPUT_VIBRATION*) { return ERROR_DEVICE_NOT_CONNECTED; }

Input::~Input()
{
    UnloadXInput();
}

bool Input::Initialize(HWND hwnd, bool useRawMouse, bool useRawKeyboard, bool captureInBackground)
{
    LoadXInput();
    return RegisterForRawInput(hwnd, useRawMouse, useRawKeyboard, captureInBackground);
}

void Input::LoadXInput()
{
    // Try modern -> legacy order for best compatibility
    // Getting started with XInput (overview) + dynamic linking pattern:
    // https://learn.microsoft.com/windows/win32/xinput/getting-started-with-xinput
    // https://learn.microsoft.com/windows/win32/dlls/using-run-time-dynamic-linking
    const wchar_t* candidates[] = {
        L"xinput1_4.dll",   // Win8+
        L"xinput1_3.dll",   // legacy DirectX redist
        L"XInput9_1_0.dll"  // Vista+
    };

    for (auto* name : candidates)
    {
        m_xinputDll = ::LoadLibraryW(name);
        if (m_xinputDll) break;
    }

    if (m_xinputDll)
    {
        m_XInputGetState = reinterpret_cast<XInputGetStateFn>(::GetProcAddress(m_xinputDll, "XInputGetState"));
        m_XInputSetState = reinterpret_cast<XInputSetStateFn>(::GetProcAddress(m_xinputDll, "XInputSetState"));
    }

    if (!m_XInputGetState) m_XInputGetState = &XInputGetStateStub;
    if (!m_XInputSetState) m_XInputSetState = &XInputSetStateStub;
}

void Input::UnloadXInput()
{
    if (m_xinputDll)
    {
        ::FreeLibrary(m_xinputDll);
        m_xinputDll = nullptr;
    }
    m_XInputGetState = &XInputGetStateStub;
    m_XInputSetState = &XInputSetStateStub;
}

bool Input::RegisterForRawInput(HWND hwnd, bool mouse, bool keyboard, bool inputSink)
{
    // To receive WM_INPUT, processes must register devices:
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerrawinputdevices
    RAWINPUTDEVICE rid[2]{};
    UINT count = 0;

    if (mouse)
    {
        rid[count].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid[count].usUsage     = HID_USAGE_GENERIC_MOUSE;
        rid[count].dwFlags     = inputSink ? RIDEV_INPUTSINK : 0; // receive even when not focused
        rid[count].hwndTarget  = hwnd;
        ++count;
    }
    if (keyboard)
    {
        rid[count].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid[count].usUsage     = HID_USAGE_GENERIC_KEYBOARD;
        rid[count].dwFlags     = inputSink ? RIDEV_INPUTSINK : 0;
        rid[count].hwndTarget  = hwnd;
        ++count;
    }
    return ::RegisterRawInputDevices(rid, count, sizeof(RAWINPUTDEVICE)) == TRUE;
}

bool Input::HandleMessage(HWND, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INPUT:
        // Retrieve RAWINPUT and dispatch to handlers:
        // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdata
        {
            UINT size = 0;
            if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
                return false;

            std::vector<BYTE> buffer(size);
            if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == size)
            {
                auto& ri = *reinterpret_cast<const RAWINPUT*>(buffer.data());
                if (ri.header.dwType == RIM_TYPEKEYBOARD) HandleRawKeyboard(ri.data.keyboard);
                else if (ri.header.dwType == RIM_TYPEMOUSE) HandleRawMouse(ri.data.mouse);
            }
        }
        return true; // Window will still call DefWindowProc for cleanup (required by WM_INPUT docs)

    case WM_MOUSEWHEEL:
        // Extra safety: some mice/drivers still raise this message
        m_mouse.wheel += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        return true;

    case WM_KILLFOCUS:
        ResetOnFocusLost();
        return false; // let window handle focus event too

    default:
        break;
    }
    return false;
}

void Input::BeginFrame()
{
    m_mouse.BeginFrame();
    m_keyboard.ClearEdges();
}

void Input::ResetOnFocusLost()
{
    m_keyboard.Reset();
    m_mouse.Reset();
    for (auto& g : m_gamepads) g = {};
}

void Input::SetDeadZones(short l, short r, unsigned char t)
{
    m_leftDead = l;
    m_rightDead = r;
    m_trigDead = t;
}

void Input::ProcessRawInput(HRAWINPUT hRawInput)
{
    UINT size = 0;
    if (::GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
        return;

    std::vector<BYTE> buffer(size);
    if (::GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        return;

    const RAWINPUT& ri = *reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (ri.header.dwType == RIM_TYPEKEYBOARD) HandleRawKeyboard(ri.data.keyboard);
    else if (ri.header.dwType == RIM_TYPEMOUSE) HandleRawMouse(ri.data.mouse);
}

UINT Input::DistinguishLeftRightShift(UINT vk, USHORT makeCode, USHORT flags)
{
    // RAWKEYBOARD.VKey is VK_SHIFT for both; need scancode -> VK_* using MAPVK_VSC_TO_VK_EX
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-mapvirtualkeyexw
    UINT sc = makeCode;
    if (flags & RI_KEY_E0) sc |= 0xE000;
    if (flags & RI_KEY_E1) sc |= 0xE100;
    UINT specific = ::MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK_EX, nullptr);
    return specific ? specific : vk;
}

void Input::HandleRawKeyboard(const RAWKEYBOARD& rk)
{
    UINT vk = rk.VKey;
    // Normalize left/right modifiers
    if (vk == VK_SHIFT)
        vk = DistinguishLeftRightShift(vk, rk.MakeCode, rk.Flags);
    else if (vk == VK_CONTROL)
        vk = (rk.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
    else if (vk == VK_MENU)
        vk = (rk.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;

    bool isDown = !(rk.Flags & RI_KEY_BREAK); // RI_KEY_BREAK means key up
    bool wasDown = m_keyboard.down[vk];

    if (isDown && !wasDown) m_keyboard.pressed[vk] = true;
    if (!isDown && wasDown) m_keyboard.released[vk] = true;
    m_keyboard.down[vk] = isDown;

    // Reference on RAWKEYBOARD flags and scancodes:
    // https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawkeyboard
}

void Input::HandleRawMouse(const RAWMOUSE& rm)
{
    if (rm.usFlags == MOUSE_MOVE_RELATIVE)
    {
        m_mouse.dx += rm.lLastX;
        m_mouse.dy += rm.lLastY;
    }
    // Buttons
    if (rm.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)   m_mouse.left = true;
    if (rm.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)     m_mouse.left = false;
    if (rm.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)  m_mouse.right = true;
    if (rm.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)    m_mouse.right = false;
    if (rm.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) m_mouse.middle = true;
    if (rm.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)   m_mouse.middle = false;
    if (rm.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)      m_mouse.x1 = true;
    if (rm.usButtonFlags & RI_MOUSE_BUTTON_4_UP)        m_mouse.x1 = false;
    if (rm.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)      m_mouse.x2 = true;
    if (rm.usButtonFlags & RI_MOUSE_BUTTON_5_UP)        m_mouse.x2 = false;
    if (rm.usButtonFlags & RI_MOUSE_WHEEL)
        m_mouse.wheel += static_cast<SHORT>(rm.usButtonData) / WHEEL_DELTA;
    if (rm.usButtonFlags & RI_MOUSE_HWHEEL)
        /* optional horizontal wheel support */ (void)0;

    // Raw Input usage pattern:
    // https://learn.microsoft.com/windows/win32/inputdev/using-raw-input
}

void Input::UpdateGamepads()
{
    // XInputGetState returns ERROR_SUCCESS on success, or ERROR_DEVICE_NOT_CONNECTED (1167) if not connected.
    // https://learn.microsoft.com/windows/win32/api/xinput/nf-xinput-xinputgetstate
    // System error code value 1167: https://learn.microsoft.com/windows/win32/debug/system-error-codes--1000-1299-
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_STATE st{};
        DWORD res = m_XInputGetState ? m_XInputGetState(i, &st) : ERROR_DEVICE_NOT_CONNECTED;

        auto& gp = m_gamepads[i];
        gp.connected = (res == ERROR_SUCCESS);
        gp.raw = st;
        gp.buttons = 0;
        gp.lx = gp.ly = gp.rx = gp.ry = gp.lt = gp.rt = 0.f;

        if (!gp.connected) continue;

        const auto dead = [](short v, short dz) -> float {
            int iv = static_cast<int>(v);
            int ad = std::abs(iv);
            if (ad <= dz) return 0.f;
            float f = (ad - dz) / static_cast<float>(32767 - dz);
            if (iv < 0) f = -f;
            return f;
        };
        const auto trig = [this](unsigned char v) -> float {
            return (v <= m_trigDead) ? 0.f : (v - m_trigDead) / static_cast<float>(255 - m_trigDead);
        };

        gp.lx = dead(st.Gamepad.sThumbLX, m_leftDead);
        gp.ly = dead(st.Gamepad.sThumbLY, m_leftDead);
        gp.rx = dead(st.Gamepad.sThumbRX, m_rightDead);
        gp.ry = dead(st.Gamepad.sThumbRY, m_rightDead);
        gp.lt = trig(st.Gamepad.bLeftTrigger);
        gp.rt = trig(st.Gamepad.bRightTrigger);
        gp.buttons = st.Gamepad.wButtons;
    }
}
