// src/input/InputWin.cpp
//
// Implementation for InputWin.h (Windows-only).
// - Raw Input registration/handling (mouse + keyboard + device change).
// - XInput dynamic loader with polling + vibration scheduling.
// - Cursor utilities.
//
// No external dependencies beyond Win32. Compile as C++17+.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "input/InputWin.h"

#include <windows.h>
#include <windowsx.h>      // GET_X_LPARAM, etc (optional)
#include <xinput.h>        // types/constants only; we dynamically load functions
#include <hidsdi.h>        // HID constants if you later expand to RAWHID
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

#pragma comment(lib, "user32.lib")

namespace cg::input {

// =============================== QPC helpers ===============================
static LARGE_INTEGER g_qpcFreq = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();

std::uint64_t InputSystem::qpc_now_()  { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (std::uint64_t)t.QuadPart; }
std::uint64_t InputSystem::qpc_freq_() { return (std::uint64_t)g_qpcFreq.QuadPart; }

// =============================== XInput loader =============================
namespace {
struct XInputAPI {
    HMODULE dll = nullptr;
    DWORD (WINAPI *GetState)(DWORD, XINPUT_STATE*) = nullptr;
    DWORD (WINAPI *SetState)(DWORD, XINPUT_VIBRATION*) = nullptr;
    DWORD (WINAPI *GetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*) = nullptr;
    DWORD (WINAPI *GetBatteryInformation)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*) = nullptr;

    void load() {
        if (dll) return;
        // Try 1.4 (Win 8+), then 1.3 (DX SDK), then 9.1.0 (Vista+), then older if present.
        const wchar_t* candidates[] = {
            L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll", L"xinput1_1.dll"
        };
        for (auto* name : candidates) {
            dll = LoadLibraryW(name);
            if (dll) break;
        }
        if (!dll) return;

        GetState  = reinterpret_cast<DWORD (WINAPI*)(DWORD, XINPUT_STATE*)>(GetProcAddress(dll, "XInputGetState"));
        SetState  = reinterpret_cast<DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*)>(GetProcAddress(dll, "XInputSetState"));
        GetCapabilities = reinterpret_cast<DWORD (WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*)>(GetProcAddress(dll, "XInputGetCapabilities"));
        GetBatteryInformation = reinterpret_cast<DWORD (WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*)>(GetProcAddress(dll, "XInputGetBatteryInformation"));
    }

    void unload() {
        if (dll) { FreeLibrary(dll); dll = nullptr; }
        GetState = nullptr; SetState = nullptr; GetCapabilities = nullptr; GetBatteryInformation = nullptr;
    }
};
static XInputAPI g_xi;
} // anon

// =============================== Raw Input helpers =========================

// Map ambiguous VKeys (Shift/Ctrl/Alt) to left/right using scancode/flags.
static UINT MapToLeftRightVK_(const RAWKEYBOARD& rk) {
    UINT vk = rk.VKey;
    const bool e0 = (rk.Flags & RI_KEY_E0) != 0;
    switch (vk) {
        case VK_SHIFT:
            // Use scancode -> VK_LSHIFT/VK_RSHIFT
            return MapVirtualKeyW(rk.MakeCode, MAPVK_VSC_TO_VK_EX);
        case VK_CONTROL: return e0 ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU:    return e0 ? VK_RMENU    : VK_LMENU;
        default: return vk;
    }
}

// =============================== InputSystem impl ==========================

bool InputSystem::initialize(HWND hwnd, const RawInputOptions& rio, const DeadzoneOptions& dz) {
    rio_ = rio; dz_ = dz;
    g_xi.load(); // safe on any Windows; function pointers may be null on very old systems
    return registerRawInput_(hwnd);
}

void InputSystem::shutdown() {
    unregisterRawInput_();
    // stop rumble on exit if possible
    if (g_xi.SetState) {
        for (int i = 0; i < kMaxGamepads; ++i) {
            XINPUT_VIBRATION v{}; g_xi.SetState(i, &v);
        }
    }
    g_xi.unload();
}

bool InputSystem::registerRawInput_(HWND hwnd) {
    RAWINPUTDEVICE rids[2]{};

    // Mouse: UsagePage=GenericDesktop(0x01), Usage=Mouse(0x02)
    rids[0].usUsagePage = 0x01;
    rids[0].usUsage     = 0x02;
    rids[0].dwFlags     = RIDEV_DEVNOTIFY; // always request device add/remove notifications
    if (rio_.no_legacy_messages) rids[0].dwFlags |= RIDEV_NOLEGACY;
    if (rio_.background)          rids[0].dwFlags |= RIDEV_INPUTSINK;
    rids[0].hwndTarget = hwnd;

    // Keyboard: UsagePage=GenericDesktop(0x01), Usage=Keyboard(0x06)
    rids[1].usUsagePage = 0x01;
    rids[1].usUsage     = 0x06;
    rids[1].dwFlags     = RIDEV_DEVNOTIFY;
    if (rio_.no_legacy_messages) rids[1].dwFlags |= RIDEV_NOLEGACY;
    if (rio_.background)          rids[1].dwFlags |= RIDEV_INPUTSINK;
    rids[1].hwndTarget = hwnd;

    registered_ = RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
    return registered_;
}

bool InputSystem::unregisterRawInput_() {
    if (!registered_) return true;
    RAWINPUTDEVICE rids[2]{};

    rids[0].usUsagePage = 0x01; rids[0].usUsage = 0x02; rids[0].dwFlags = RIDEV_REMOVE; rids[0].hwndTarget = nullptr;
    rids[1].usUsagePage = 0x01; rids[1].usUsage = 0x06; rids[1].dwFlags = RIDEV_REMOVE; rids[1].hwndTarget = nullptr;

    BOOL ok = RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));
    registered_ = false;
    return ok != FALSE;
}

bool InputSystem::reregisterRawInput(HWND hwnd) {
    unregisterRawInput_();
    return registerRawInput_(hwnd);
}

void InputSystem::setDeadzones(const DeadzoneOptions& dz) { dz_ = dz; }

bool InputSystem::processMessage(HWND hwnd, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam) {
    switch (msg) {
    case WM_INPUT:
        // NOTE: Even if we handle it, the app should still forward WM_INPUT/RIM_INPUT to DefWindowProc
        // for system cleanup as per Microsoft docs.
        return handleRawInput_(lParam);
    case WM_INPUT_DEVICE_CHANGE:
        handleDeviceChange_(wParam, lParam);
        return true;
    case WM_ACTIVATE:
        // Optionally re-confine when reactivated if needed.
        if (state_.mouse.relativeMode || state_.mouse.confined) confineCursorToWindow(hwnd, true);
        return false;
    default:
        break;
    }
    return false;
}

void InputSystem::newFrame() {
    // Reset per-frame mouse deltas/wheels.
    state_.mouse.dx = 0; state_.mouse.dy = 0;
    state_.mouse.wheel = 0; state_.mouse.wheelH = 0;

    // Poll pads + manage rumble lifetime.
    pollGamepads();
}

void InputSystem::pollGamepads() {
    g_xi.load(); // no-op if already loaded
    for (int i = 0; i < kMaxGamepads; ++i) {
        pollSinglePad_(i);

        // Rumble expiry
        if (g_xi.SetState) {
            auto& r = rumble_[i];
            if (r.end_tick && qpc_now_() >= r.end_tick) {
                r.end_tick = 0; r.left = r.right = 0;
                XINPUT_VIBRATION v{}; g_xi.SetState(i, &v);
            }
        }
    }
}

void InputSystem::pollSinglePad_(int idx) {
    auto& pad = state_.pads[(size_t)idx];

    if (!g_xi.GetState) {
        pad.connected = false;
        return;
    }

    XINPUT_STATE s{};
    DWORD rc = g_xi.GetState(idx, &s);
    if (rc != ERROR_SUCCESS) {
        pad.connected = false;
        return;
    }

    pad.connected = true;

    const XINPUT_GAMEPAD& gp = s.Gamepad;
    pad.buttons = gp.wButtons;

    // Raw reads
    pad.lt_raw = gp.bLeftTrigger;  pad.rt_raw = gp.bRightTrigger;
    pad.lx_raw = gp.sThumbLX;      pad.ly_raw = gp.sThumbLY;
    pad.rx_raw = gp.sThumbRX;      pad.ry_raw = gp.sThumbRY;

    // Normalize triggers (0..1) w/ dead-zone
    auto normT = [](uint8_t v) -> float { return float(v) / 255.0f; };
    pad.lt = applyTriggerDeadzone_(normT(pad.lt_raw), dz_.trigger);
    pad.rt = applyTriggerDeadzone_(normT(pad.rt_raw), dz_.trigger);

    // Sticks: normalize to [-1..1], then apply dead-zone
    auto normS = [](int16_t v) -> float {
        // avoid 32768 overflow: use 32767.0f for both signs; clamp
        float f = (v >= 0) ? (float(v) / 32767.0f) : (float(v) / 32768.0f);
        return std::clamp(f, -1.0f, 1.0f);
    };

    float lx = normS(pad.lx_raw), ly = normS(pad.ly_raw);
    float rx = normS(pad.rx_raw), ry = normS(pad.ry_raw);
    applyStickDeadzone_(lx, ly, dz_.left_stick,  dz_.radial_sticks, pad.lx, pad.ly);
    applyStickDeadzone_(rx, ry, dz_.right_stick, dz_.radial_sticks, pad.rx, pad.ry);

    // Optionally query caps/subtype once (cheap on 1.4/1.3; 9.1.0 returns fixed GAMEPAD)
    if (g_xi.GetCapabilities) {
        XINPUT_CAPABILITIES caps{};
        if (g_xi.GetCapabilities(idx, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) {
            pad.subtype = caps.SubType;
            pad.capsFlags = caps.Flags;
        }
    }
}

void InputSystem::applyStickDeadzone_(float inX, float inY, float dz, bool radial,
                                      float& outX, float& outY) {
    if (!radial) {
        auto f = [dz](float v) {
            float a = std::fabs(v);
            if (a <= dz) return 0.0f;
            float t = (a - dz) / (1.0f - dz);
            return (v < 0.0f) ? -t : t;
        };
        outX = f(inX); outY = f(inY);
        return;
    }
    // radial: preserve direction; scale magnitude to [0..1] post-deadzone
    float mag = std::sqrt(inX*inX + inY*inY);
    if (mag <= dz) { outX = outY = 0.0f; return; }
    float newMag = (mag - dz) / (1.0f - dz);
    float scale = (mag > 0.0001f) ? (newMag / mag) : 0.0f;
    outX = inX * scale; outY = inY * scale;
}

float InputSystem::applyTriggerDeadzone_(float v, float dz) const {
    if (v <= dz) return 0.0f;
    return (v - dz) / (1.0f - dz);
}

bool InputSystem::setGamepadVibration(int padIndex, float left, float right, uint32_t duration_ms) {
    if (padIndex < 0 || padIndex >= kMaxGamepads || !g_xi.SetState) return false;
    // clamp to [0..1] -> [0..65535]
    auto cvt = [](float f) -> WORD {
        f = std::clamp(f, 0.0f, 1.0f);
        return (WORD)std::lround(f * 65535.0f);
    };
    WORD l = cvt(left), r = cvt(right);
    XINPUT_VIBRATION v{}; v.wLeftMotorSpeed = l; v.wRightMotorSpeed = r;
    if (g_xi.SetState(padIndex, &v) != ERROR_SUCCESS) return false;

    auto& rr = rumble_[(size_t)padIndex];
    rr.left = l; rr.right = r;
    rr.end_tick = (duration_ms == 0) ? 0 :
                  (qpc_now_() + (std::uint64_t)((qpc_freq_() * (double)duration_ms) / 1000.0));
    return true;
}

void InputSystem::stopGamepadVibration(int padIndex) {
    if (padIndex < 0 || padIndex >= kMaxGamepads || !g_xi.SetState) return;
    XINPUT_VIBRATION v{}; g_xi.SetState(padIndex, &v);
    auto& rr = rumble_[(size_t)padIndex];
    rr.left = rr.right = 0; rr.end_tick = 0;
}

// ------------------------------- Cursor helpers ----------------------------

bool InputSystem::confineCursorToWindow(HWND hwnd, bool enable) {
    if (!hwnd) return false;
    if (enable) {
        RECT r{}; if (!GetClientRect(hwnd, &r)) return false;
        POINT tl{ r.left, r.top }, br{ r.right, r.bottom };
        ClientToScreen(hwnd, &tl); ClientToScreen(hwnd, &br);
        RECT sr{ tl.x, tl.y, br.x, br.y };
        if (!ClipCursor(&sr)) return false;
        state_.mouse.confined = true;
        return true;
    } else {
        if (!ClipCursor(nullptr)) return false;
        state_.mouse.confined = false;
        return true;
    }
}

void InputSystem::centerCursor(HWND hwnd) {
    if (!hwnd) return;
    RECT r{}; if (!GetClientRect(hwnd, &r)) return;
    POINT c{ (r.left + r.right)/2, (r.top + r.bottom)/2 };
    ClientToScreen(hwnd, &c);
    SetCursorPos(c.x, c.y);
}

void InputSystem::setCursorVisible(bool visible) {
    // ShowCursor uses a display count. ≥0 shows, <0 hides. Drive it to desired state.
    CURSORINFO ci{ sizeof(CURSORINFO) };
    bool currentlyVisible = (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING));
    if (visible == currentlyVisible) return;

    if (visible) {
        while (ShowCursor(TRUE) < 0) {/* keep incrementing */}
    } else {
        while (ShowCursor(FALSE) >= 0) {/* keep decrementing */}
    }
}

bool InputSystem::setRelativeMouseMode(HWND hwnd, bool enable) {
    if (enable == state_.mouse.relativeMode) return true;
    if (enable) {
        setCursorVisible(false);
        if (!confineCursorToWindow(hwnd, true)) return false;
        centerCursor(hwnd);
        state_.mouse.relativeMode = true;
    } else {
        confineCursorToWindow(hwnd, false);
        setCursorVisible(true);
        state_.mouse.relativeMode = false;
    }
    return true;
}

// ------------------------------- WM_INPUT handling -------------------------

bool InputSystem::handleRawInput_(std::intptr_t lParam) {
    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        return false;

    std::vector<BYTE> buf(size);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        return false;

    RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buf.data());
    if (ri->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = ri->data.mouse;

        // Movement: relative or absolute
        if (m.usFlags & MOUSE_MOVE_RELATIVE) {
            state_.mouse.dx += (int)m.lLastX;
            state_.mouse.dy += (int)m.lLastY;
        } else if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
            // Some devices (pen/touch) report absolute; treat as deltas if needed.
            // For pure relative gameplay, you may ignore absolute moves.
            // Here we just accumulate as deltas.
            state_.mouse.dx += (int)m.lLastX;
            state_.mouse.dy += (int)m.lLastY;
        }

        // Buttons
        USHORT bf = m.usButtonFlags;
        if (bf & RI_MOUSE_BUTTON_1_DOWN) state_.mouse.left   = true;
        if (bf & RI_MOUSE_BUTTON_1_UP)   state_.mouse.left   = false;
        if (bf & RI_MOUSE_BUTTON_2_DOWN) state_.mouse.right  = true;
        if (bf & RI_MOUSE_BUTTON_2_UP)   state_.mouse.right  = false;
        if (bf & RI_MOUSE_BUTTON_3_DOWN) state_.mouse.middle = true;
        if (bf & RI_MOUSE_BUTTON_3_UP)   state_.mouse.middle = false;
        if (bf & RI_MOUSE_BUTTON_4_DOWN) state_.mouse.x1     = true;
        if (bf & RI_MOUSE_BUTTON_4_UP)   state_.mouse.x1     = false;
        if (bf & RI_MOUSE_BUTTON_5_DOWN) state_.mouse.x2     = true;
        if (bf & RI_MOUSE_BUTTON_5_UP)   state_.mouse.x2     = false;

        // Wheel: vertical + horizontal (tilt)
        if (bf & RI_MOUSE_WHEEL) {
            // usButtonData is WHEEL_DELTA (+/-120) in high word; RAWMOUSE packs it as USHORT
            // reinterpret as signed short
            state_.mouse.wheel += (short)m.usButtonData;
        }
        if (bf & RI_MOUSE_HWHEEL) {
            state_.mouse.wheelH += (short)m.usButtonData;
        }

        // Optionally sample absolute cursor (useful when not in relative mode).
        if (rio_.track_cursor) {
            POINT p; if (GetCursorPos(&p)) {
                // In case hwnd changed, we can't map here. Caller can set track_cursor=false and
                // track x/y in app if needed.
                // For completeness, try to infer target from foreground window:
                HWND f = GetForegroundWindow();
                if (f) {
                    ScreenToClient(f, &p);
                    state_.mouse.x = p.x;
                    state_.mouse.y = p.y;
                }
            }
        }
        return true;
    }
    else if (ri->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& k = ri->data.keyboard;

        // Some "fake" keys use VKey=255; ignore these.
        if (k.VKey == 255) return true;

        UINT vk = MapToLeftRightVK_(k);

        const bool keyUp = (k.Flags & RI_KEY_BREAK) != 0;
        if (vk < 256) {
            state_.keyboard.down.set(vk, !keyUp);
        }
        return true;
    }
    // RIM_TYPEHID (other HIDs) ignored for now.
    return false;
}

void InputSystem::handleDeviceChange_(std::uintptr_t wParam, std::intptr_t /*lParam*/) {
    // GIDC_ARRIVAL (1) / GIDC_REMOVAL (2). You could refresh device lists here, or just ignore.
    // We keep Raw Input registration persistent; nothing to do for basic mouse/keyboard.
    (void)wParam;
}

} // namespace cg::input
