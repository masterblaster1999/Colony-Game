#pragma once
//
// Windows-only Raw Input header for keyboard + mouse.
// Matches the API implemented in platform/win/InputRaw.cpp.
//
// Usage:
//   input::InputRaw input(hwnd);
//   input::InputRaw::Options opts;
//   opts.receiveWhenUnfocused = true;  // RIDEV_INPUTSINK
//   opts.notifyDeviceChanges  = true;  // WM_INPUT_DEVICE_CHANGE
//   input.initialize(opts);
//
// In your WndProc:
//   LRESULT defRes = 0;
//   if (input.handleMessage(hwnd, msg, wParam, lParam, defRes))
//       return defRes; // WM_INPUT cleanup already handled.
//

#if !defined(_WIN32)
#  error "InputRaw is Windows-only."
#endif

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>   // HWND, HRAWINPUT, WPARAM, LPARAM, RAWINPUT, etc.
#include <functional>  // std::function
#include <deque>       // std::deque event queue (if no sink is set)
#include <mutex>       // std::mutex guarding the queue

namespace input {

// High-level input event delivered by Raw Input backend.
// If you prefer, map this onto your engine's internal event types at one place.
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

  // Common metadata
  HANDLE device     = nullptr; // Raw input device handle (may be nullptr)
  UINT   timestamp  = 0;       // GetMessageTime() snapshot

  // Keyboard
  UINT   vkey       = 0;       // Normalized VK_* (e.g., VK_LSHIFT/VK_RSHIFT)
  USHORT scanCode   = 0;       // Hardware scan code
  bool   extended   = false;   // E0/E1 extended key info (true if E0 or E1 set)

  // Mouse
  LONG   mouseDX    = 0;       // Relative delta X (or absolute, see 'absolute')
  LONG   mouseDY    = 0;       // Relative delta Y
  bool   absolute   = false;   // True if device reported absolute coordinates
  int    mouseButton = -1;     // 0=L,1=R,2=M,3=X1,4=X2 (for button events)
  int    wheelDelta  = 0;      // +/−120 per detent (or multiples)

  // Convenience factories (do not allocate; just fill a struct)
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
    e.type = down ? Type::MouseButtonDown : Type::MouseButtonUp;
    e.mouseButton = button; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeWheel(bool horizontal, int delta, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = horizontal ? Type::MouseHWheel : Type::MouseWheel;
    e.wheelDelta = delta; e.device = dev; e.timestamp = ts;
    return e;
  }
  static InputEvent MakeDevice(bool arrived, HANDLE dev, UINT ts) {
    InputEvent e{};
    e.type = arrived ? Type::DeviceArrived : Type::DeviceRemoved;
    e.device = dev; e.timestamp = ts;
    return e;
  }
};

// Raw Input backend: registers devices, parses WM_INPUT, and delivers InputEvent.
// You can consume events via a callback sink, or poll from the internal queue.
class InputRaw {
public:
  struct Options {
    // Receive input even when the window is not focused (RIDEV_INPUTSINK).
    bool receiveWhenUnfocused = true;

    // Receive WM_INPUT_DEVICE_CHANGE notifications (RIDEV_DEVNOTIFY).
    bool notifyDeviceChanges  = true;

    // Suppress legacy WM_* keyboard messages (RIDEV_NOLEGACY). Keep 'false'
    // if you still rely on WM_CHAR for text input alongside Raw Input.
    bool noLegacyKeyboard     = false;

    // Suppress legacy WM_* mouse button messages (RIDEV_NOLEGACY).
    bool noLegacyMouse        = false;
  };

  using Sink = std::function<void(const InputEvent&)>;

  explicit InputRaw(HWND hwnd) : m_hwnd(hwnd) {}
  ~InputRaw() = default;

  // Register for raw keyboard + mouse according to Options.
  // Returns false if RegisterRawInputDevices fails.
  bool initialize(const Options& opts);

  // Optional: unregister devices (RIDEV_REMOVE).
  void shutdown();

  // Forward messages from your WndProc here.
  // If it returns true, set your WndProc return value to 'defResult'
  // (WM_INPUT cleanup is already handled inside).
  bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& defResult);

  // If you don't set a sink, you can pull events each frame with pollEvent().
  void setSink(Sink s) { m_sink = std::move(s); }
  bool pollEvent(InputEvent& out);

private:
  // Internal helpers (implemented in .cpp)
  void push(const InputEvent& e);
  void processRawInput(HRAWINPUT hri, WPARAM wParam);
  static UINT normalizeVK(UINT vkey, USHORT scanCode, bool e0, bool e1);

private:
  HWND               m_hwnd = nullptr;
  Options            m_opts{};
  std::deque<InputEvent> m_queue;
  Sink               m_sink;
  std::mutex         m_qMutex;
};

} // namespace input
