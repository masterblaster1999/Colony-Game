# Patch 6: Configurable multi-binds + chords + mouse button bindings

This patch builds on the previous input decoupling work and makes input bindings fully data-driven.

## What this patch adds

- **Multiple bindings per action via JSON / INI**
  - New optional config files:
    - `assets/config/input_bindings.json`
    - `assets/config/input_bindings.ini`
  - If no file is found, the compiled-in defaults remain in use.

- **Action chords** (e.g., `Shift+W` as a distinct action)
  - Any binding string can use `+` to form a chord.
  - Example: `MoveForwardFast = Shift+W`

- **Mouse button bindings in the same mapper**
  - Mouse buttons are treated as bindable inputs (like keys).
  - New camera actions:
    - `CameraOrbit` (default: `MouseLeft`)
    - `CameraPan`   (default: `MouseMiddle`, `MouseRight`, and `Shift+MouseLeft`)

## What changed

- `src/input/InputEvent.h`
  - Adds `MouseButtonDown` / `MouseButtonUp` event types.
  - Defines a unified input code space so mouse buttons can be mapped alongside keyboard keys.

- `src/AppWindow.cpp`
  - Emits mouse button down/up events into the input queue (`WM_LBUTTON*`, `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_XBUTTON*`).
  - Forwards `WM_SYSKEYDOWN/UP` (notably Alt) into the input queue so chords like `Alt+MouseLeft` can be bound.
  - Still keeps all window/system toggles in the window layer (Esc, V, F11, Alt+Enter, etc.).

- `src/input/InputMapper.h/.cpp`
  - Supports:
    - multiple bindings per action
    - chord bindings (`Shift+W`, `Shift+MouseLeft`, etc.)
    - optional JSON/INI binding overrides
    - mouse buttons as input codes

- `src/game/PrototypeGame.cpp`
  - Camera orbit/pan is now **100% action-driven**.
  - Loads bindings from `assets/config/input_bindings.json` / `.ini` automatically (best-effort search).

## Default controls (same behavior as before)

- Camera movement:
  - **WASD** or **Arrow keys** = pan
  - **Q/E** = zoom
  - **Shift** = speed boost

- Camera mouse:
  - **LMB drag** = orbit
  - **MMB / RMB drag** = pan
  - **Shift + LMB drag** = pan (chord example)

# Patch 7: Window + input quality-of-life

This patch focuses on practical Windows-only improvements that make the prototype feel better during real use.

## What this patch adds

- **Pause in background (Alt+Tab) support**
  - New user settings in `%LOCALAPPDATA%\ColonyGame\settings.json`:
    - `runtime.pauseWhenUnfocused` (default `true`)
    - `runtime.maxFpsWhenUnfocused` (default `30`, used when `pauseWhenUnfocused` is `false`)

- **Smoother resizing**
  - Swapchain resizing is deferred while the user is dragging the window border
    (`WM_ENTERSIZEMOVE` .. `WM_EXITSIZEMOVE`), so we only resize once at the final size.

- **XButton (Mouse4/Mouse5) drag support**
  - Mouse X1 / X2 now participate in the drag/capture logic, so actions like camera orbit/pan can be bound to `MouseX1` or `MouseX2` and still receive mouse delta.

## Bug fixes

- Fixes a potential out-of-bounds write in `src/main_win.cpp` when extracting dropped file paths via `DragQueryFileW`.


# Patch 8: Latency/resize guardrails + robustness fixes

This patch tightens up a few Windows prototype fundamentals: swapchain latency control, resize guardrails, and crash/worker robustness.

## What this patch adds

- **DXGI max frame latency hotkey (F8)**
  - Cycles `settings.maxFrameLatency` from **1..16** at runtime.
  - Applies immediately via `DxDevice::SetMaxFrameLatency`.

- **Pause-when-unfocused hotkey (F7)**
  - Toggles `runtime.pauseWhenUnfocused` without restarting.

- **Minimum window size enforcement**
  - Adds `WM_GETMINMAXINFO` handling so the user cannot resize the client area below:
    - **640×360** (DPI-aware).

- **More informative window title**
  - Title now shows: `Lat`, `Raw`, and `PauseBG` states.

## Bug fixes / robustness

- `src/core/JobSystem.cpp`
  - Adds missing `<algorithm>` include (fixes potential build break on strict toolchains).
  - Wraps job execution in `try/catch` so a single throwing job can't terminate the worker thread.

- `src/core/Crash.cpp`
  - Avoids calling `MiniDumpWriteDump` with an invalid file handle.
  - Uses non-throwing `std::filesystem::create_directories(..., error_code)` in the crash path.

- `src/DxDevice.cpp`
  - Ensures `RSSetViewports` / `RSSetScissorRects` match swapchain size each frame.

- `src/UserSettings.cpp`
  - Separates window width/height clamping (min height now **360** instead of forcing **640**).
  - `ReadFileToString` now verifies the full file was read.
