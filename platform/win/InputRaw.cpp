// platform/win/InputRaw.cpp
// Windows-only Raw Input backend for keyboard + mouse.
// - Registers for raw keyboard & mouse (HID Generic Desktop page).
// - Decodes scan codes, extended keys (E0/E1), make/break.
// - Decodes mouse relative/absolute motion, buttons, wheel + horizontal wheel.
// - Optional input while unfocused (INPUTSINK) and device change notifications.
// - Queues events or invokes a user-provided sink (callback).
//
// This .cpp is self-contained. If you already have an InputRaw.h that declares
// the same API, it will be used. Otherwise a minimal shim is provided below
// so this translation unit can compile standalone. Adjust to your project
// conventions as needed.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>   // Some SDKs pull in hidusage via this; include both to be safe.
#include <hidusage.h> // HID_USAGE_PAGE_GENERIC / HID_USAGE_GENERIC_MOUSE / KEYBOARD
#include <shellscalingapi.h> // (not required here, but common in your bootstrap)
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <cassert>

#pragma comment(lib, "User32.lib")

// ---------- Minimal header shim (used only if you don't already have InputRaw.h) ----------
#if __has_include("InputRaw.h")
  #include "InputRaw.h"
#else
namespace input {

// Basic event model; adapt as needed to match your engine's types.
struct InputEvent {
  enum class Type {
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    MouseHWheel,
    KeyDown,
    KeyUp,
    DeviceArrived,
    DeviceRemoved
  } type;

  // Common
  HANDLE      device = nullptr; // Raw input device handle (can be nullptr)
  UINT        timestamp = 0;    // GetMessageTime()

  // Keyboard
  UINT        vkey = 0;         // VK_* after normalization (e.g., VK_LSHIFT)
  USHORT      scanCode = 0;     // Hardware scan code
  bool        extended = false; // E0/E1 flag insight

  // Mouse
  LONG        mouseDX = 0;      // Relative or absolute (see flags)
  LONG        mouseDY = 0;
  bool        absolute = false; // True if the event reported absolute coords
  int         mouseButton = -1; // 0=L,1=R,2=M,3=X1,4=X2 for button events
  int         wheelDelta = 0;   // 120 units per wheel detent (or multiples)

  // Convenience
  static InputEvent MakeKey(bool down, UINT vk, USHORT sc, bool ext, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = down ? Type::KeyDown : Type::KeyUp;
    e.vkey = vk; e.scanCode = sc; e.extended = ext; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeMouseMove(LONG dx, LONG dy, bool abs, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = Type::MouseMove; e.mouseDX = dx; e.mouseDY = dy; e.absolute = abs; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeMouseButton(bool down, int button, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = down ? Type::MouseButtonDown : Type::MouseButtonUp; e.mouseButton = button; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeWheel(bool horizontal, int delta, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = horizontal ? Type::MouseHWheel : Type::MouseWheel; e.wheelDelta = delta; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeDevice(bool arrived, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = arrived ? Type::DeviceArrived : Type::DeviceRemoved; e.device = dev; e.timestamp = ts;
    return e;
  }
};

class InputRaw {
public:
  struct Options {
    bool receiveWhenUnfocused = true;  // RIDEV_INPUTSINK
    bool notifyDeviceChanges  = true;  // RIDEV_DEVNOTIFY
    bool noLegacyKeyboard     = false; // RIDEV_NOLEGACY (prevents WM_KEYDOWN/UP)
    bool noLegacyMouse        = false; // RIDEV_NOLEGACY (prevents WM_*BUTTON*)
  };

  using Sink = std::function<void(const InputEvent&)>;

  explicit InputRaw(HWND hwnd) : m_hwnd(hwnd) {}
  ~InputRaw() = default;

  bool initialize(const Options& opts);
  void shutdown();

  // Forward messages here. If returns true, set your WndProc return to 'defResult'.
  bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& defResult);

  // If you don't use a callback sink, pull events via pollEvent().
  void setSink(Sink s) { m_sink = std::move(s); }
  bool pollEvent(InputEvent& out);

private:
  void push(const InputEvent& e);
  void processRawInput(HRAWINPUT hri, WPARAM wParam);

  // Helpers
  static UINT normalizeVK(UINT vkey, USHORT scanCode, bool e0, bool e1);

private:
  HWND                 m_hwnd = nullptr;
  Options              m_opts{};
  std::deque<InputEvent> m_queue;
  Sink                 m_sink;
  std::mutex           m_qMutex; // Only needed if you deliver from a different thread (normally not).
};

} // namespace input
#endif // shim
// -----------------------------------------------------------------------------------------

using namespace input;

// Small debug logger (optional). Replace with your project logger if desired.
static void DebugLog(const wchar_t* msg) {
#if defined(_DEBUG)
  OutputDebugStringW(msg);
#else
  (void)msg;
#endif
}

// Map Raw Input mouse button flags to our 0..4 indices.
static inline std::optional<int> ButtonFromFlags(USHORT fDown, USHORT fUp, USHORT flags) {
  // Down?
  if (flags & fDown) {
    if (fDown == RI_MOUSE_LEFT_BUTTON_DOWN)   return 0;
    if (fDown == RI_MOUSE_RIGHT_BUTTON_DOWN)  return 1;
    if (fDown == RI_MOUSE_MIDDLE_BUTTON_DOWN) return 2;
    if (fDown == RI_MOUSE_BUTTON_4_DOWN)      return 3;
    if (fDown == RI_MOUSE_BUTTON_5_DOWN)      return 4;
  }
  // Up?
  if (flags & fUp) {
    if (fUp == RI_MOUSE_LEFT_BUTTON_UP)   return 0;
    if (fUp == RI_MOUSE_RIGHT_BUTTON_UP)  return 1;
    if (fUp == RI_MOUSE_MIDDLE_BUTTON_UP) return 2;
    if (fUp == RI_MOUSE_BUTTON_4_UP)      return 3;
    if (fUp == RI_MOUSE_BUTTON_5_UP)      return 4;
  }
  return std::nullopt;
}

// Normalize VK to disambiguate L/R SHIFT/CTRL/ALT for Raw Input.
// Based on MSDN guidance: use scan code for SHIFT; E0 for CTRL/ALT.
UINT InputRaw::normalizeVK(UINT vkey, USHORT scanCode, bool e0, bool /*e1*/) {
  if (vkey == VK_SHIFT)   { vkey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX); } // LSHIFT/RSHIFT
  else if (vkey == VK_CONTROL) { vkey = e0 ? VK_RCONTROL : VK_LCONTROL; }
  else if (vkey == VK_MENU)    { vkey = e0 ? VK_RMENU    : VK_LMENU; }
  return vkey;
}

bool InputRaw::initialize(const Options& opts) {
  m_opts = opts;

  RAWINPUTDEVICE rid[2];
  ZeroMemory(rid, sizeof(rid));

  // Mouse
  rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;   // 0x01
  rid[0].usUsage     = HID_USAGE_GENERIC_MOUSE;  // 0x02
  rid[0].hwndTarget  = m_hwnd;
  rid[0].dwFlags     = 0;
  if (m_opts.receiveWhenUnfocused) rid[0].dwFlags |= RIDEV_INPUTSINK;
  if (m_opts.notifyDeviceChanges)  rid[0].dwFlags |= RIDEV_DEVNOTIFY;
  if (m_opts.noLegacyMouse)        rid[0].dwFlags |= RIDEV_NOLEGACY;

  // Keyboard
  rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;   // 0x01
  rid[1].usUsage     = HID_USAGE_GENERIC_KEYBOARD; // 0x06
  rid[1].hwndTarget  = m_hwnd;
  rid[1].dwFlags     = 0;
  if (m_opts.receiveWhenUnfocused) rid[1].dwFlags |= RIDEV_INPUTSINK;
  if (m_opts.notifyDeviceChanges)  rid[1].dwFlags |= RIDEV_DEVNOTIFY;
  if (m_opts.noLegacyKeyboard)     rid[1].dwFlags |= RIDEV_NOLEGACY;

  if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
    DWORD err = GetLastError();
    wchar_t buf[128];
    swprintf_s(buf, L"[InputRaw] RegisterRawInputDevices failed. GetLastError=%lu\n", err);
    DebugLog(buf);
    return false;
  }

  DebugLog(L"[InputRaw] Raw Input initialized.\n");
  return true;
}

void InputRaw::shutdown() {
  // Optional: unregister by passing RIDEV_REMOVE
  RAWINPUTDEVICE rid[2]{};
  rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;  rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
  rid[0].dwFlags = RIDEV_REMOVE;                rid[0].hwndTarget = nullptr;

  rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;  rid[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
  rid[1].dwFlags = RIDEV_REMOVE;                rid[1].hwndTarget = nullptr;
  RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

bool InputRaw::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& defResult) {
  // We only consume WM_INPUT and WM_INPUT_DEVICE_CHANGE. Everything else: not handled.
  switch (msg) {
    case WM_INPUT: {
      processRawInput(reinterpret_cast<HRAWINPUT>(lParam), wParam);

      // IMPORTANT per docs: call DefWindowProc for WM_INPUT when foreground to let the system clean up.
      // We'll do that here and report "handled".
      defResult = DefWindowProcW(hwnd, msg, wParam, lParam);
      return true;
    }
    case WM_INPUT_DEVICE_CHANGE: {
      const bool arrived = (wParam == GIDC_ARRIVAL);
      // lParam is the HANDLE to the device that was added/removed.
      push(InputEvent::MakeDevice(arrived, reinterpret_cast<HANDLE>(lParam), GetMessageTime()));
      defResult = 0; // no need to call DefWindowProc for this one
      return true;
    }
    default:
      break;
  }
  return false;
}

bool InputRaw::pollEvent(InputEvent& out) {
  std::lock_guard<std::mutex> lock(m_qMutex);
  if (m_queue.empty()) return false;
  out = std::move(m_queue.front());
  m_queue.pop_front();
  return true;
}

void InputRaw::push(const InputEvent& e) {
  if (m_sink) {
    m_sink(e);
  } else {
    std::lock_guard<std::mutex> lock(m_qMutex);
    m_queue.push_back(e);
  }
}

void InputRaw::processRawInput(HRAWINPUT hri, WPARAM wParam) {
  // Get required size
  UINT size = 0;
  if (GetRawInputData(hri, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
    return; // nothing to read
  }
  std::vector<BYTE> buffer(size);
  if (GetRawInputData(hri, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
    return; // read failed
  }

  RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buffer.data());
  const HANDLE hDevice = ri->header.hDevice;
  const UINT   ts      = GetMessageTime();
  (void)wParam; // Foreground/background info is available via GET_RAWINPUT_CODE_WPARAM(wParam) if you need it.

  if (ri->header.dwType == RIM_TYPEMOUSE) {
    const RAWMOUSE& rm = ri->data.mouse;

    // Motion
    bool absolute = (rm.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    LONG dx = 0, dy = 0;
    if (absolute) {
      // Typically seen on some tablets/VMs; interpret as absolute deltas (caller can map them).
      dx = rm.lLastX; dy = rm.lLastY;
    } else {
      dx = rm.lLastX; dy = rm.lLastY;
    }
    if (dx != 0 || dy != 0) {
      push(InputEvent::MakeMouseMove(dx, dy, absolute, hDevice, ts));
    }

    // Buttons (down/up)
    const USHORT f = rm.usButtonFlags;

    if (auto b = ButtonFromFlags(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, f))     push(InputEvent::MakeMouseButton((f & RI_MOUSE_LEFT_BUTTON_DOWN)!=0,  *b, hDevice, ts));
    if (auto b = ButtonFromFlags(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, f))   push(InputEvent::MakeMouseButton((f & RI_MOUSE_RIGHT_BUTTON_DOWN)!=0, *b, hDevice, ts));
    if (auto b = ButtonFromFlags(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, f)) push(InputEvent::MakeMouseButton((f & RI_MOUSE_MIDDLE_BUTTON_DOWN)!=0,*b, hDevice, ts));
    if (auto b = ButtonFromFlags(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, f))           push(InputEvent::MakeMouseButton((f & RI_MOUSE_BUTTON_4_DOWN)!=0,     *b, hDevice, ts));
    if (auto b = ButtonFromFlags(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, f))           push(InputEvent::MakeMouseButton((f & RI_MOUSE_BUTTON_5_DOWN)!=0,     *b, hDevice, ts));

    // Wheel & horizontal wheel
    if (f & RI_MOUSE_WHEEL) {
      // usButtonData is a SIGNED value; +120/-120 per detent (or multiples)
      const SHORT wheel = static_cast<SHORT>(rm.usButtonData);
      push(InputEvent::MakeWheel(false, static_cast<int>(wheel), hDevice, ts));
    }
    if (f & RI_MOUSE_HWHEEL) {
      const SHORT wheel = static_cast<SHORT>(rm.usButtonData);
      push(InputEvent::MakeWheel(true, static_cast<int>(wheel), hDevice, ts));
    }
  }
  else if (ri->header.dwType == RIM_TYPEKEYBOARD) {
    const RAWKEYBOARD& rk = ri->data.keyboard;

    // Some keyboards can generate 0xFF "fake" vkeys—ignore them.
    if (rk.VKey == 0xFF) return;

    const bool isBreak = (rk.Flags & RI_KEY_BREAK) != 0;   // up (break) vs down (make)
    const bool e0      = (rk.Flags & RI_KEY_E0) != 0;      // extended key prefixes
    const bool e1      = (rk.Flags & RI_KEY_E1) != 0;

    UINT   vkey = normalizeVK(rk.VKey, rk.MakeCode, e0, e1);
    USHORT sc   = rk.MakeCode;

    push(InputEvent::MakeKey(!isBreak, vkey, sc, e0 || e1, hDevice, ts));
  }
  // else: RI HID devices — not handled here.
}
