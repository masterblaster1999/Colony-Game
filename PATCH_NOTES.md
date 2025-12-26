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


# Patch 9: FPS cap cycling + hotkey discoverability + persistence fixes

This patch tightens the runtime hotkey story and fixes a persistence edge-case in settings.

## What this patch adds

- **FPS cap cycling hotkeys (F6 / Shift+F6)**
  - **F6**: cycles `graphics.maxFpsWhenVsyncOff` (∞ / 60 / 120 / 144 / 165 / 240)
  - **Shift+F6**: cycles `runtime.maxFpsWhenUnfocused` (∞ / 5 / 10 / 30 / 60)
  - Applies immediately via `FramePacer` and persists to `%LOCALAPPDATA%\ColonyGame\settings.json`.

- **Runtime hotkey help popup (F3)**
  - **F3** opens a simple MessageBox listing window/system hotkeys.
  - Handy when ImGui is disabled or when users want a quick refresher.

- **Title-bar improvements**
  - Title now includes `Cap` and `BGCap` so you can see the active caps at a glance.

## Bug fixes

- `src/UserSettings.cpp`
  - Fixes an inconsistency where low background FPS caps (e.g. **5** or **10**) would be clamped up to **30** on load.

- `src/AppWindow_WndProc_Input.cpp`
  - Stops forwarding key-up events for system hotkeys (F1/F2/etc.) into the input mapper.
    This avoids odd one-sided key events for keys consumed by the window layer.

- `src/input/InputMapper.cpp`
  - `ReadFileToString` now verifies the full file was read (matches the settings loader’s robustness).

# Patch 10: DXGI waitable-handle pacing correctness + responsiveness

This patch hardens the low-latency DXGI “waitable swapchain” path.

## What this patch fixes / improves

- `src/AppWindow_Loop.cpp`
  - Re-queries the swapchain frame-latency waitable handle each wait iteration.
    This fixes **undefined behavior** if the swapchain is resized or the frame latency is changed while waiting.
  - Drains any pending Win32 messages after the handle becomes signaled.
    `MsgWaitForMultipleObjectsEx` can return the signaled handle even when messages are queued, so this keeps input as fresh as possible.
  - Wait timing stats now measure **only the time spent inside the wait call** (not the time spent dispatching messages).

- `src/AppWindow_Create.cpp`
  - Adds a `WH ON/OFF` indicator to the title bar, so you can tell whether the DXGI frame-latency waitable object is available on the current system.


# Patch 11: DXGI diagnostics title overlay (no ImGui required)

This patch adds an optional one-line DXGI diagnostics segment in the window title.
It’s meant for debugging odd presentation behavior (tearing, flip-model eligibility, waitable swapchains, etc.).

## What this patch adds

- **DXGI diagnostics toggle (F12)**
  - Toggles `debug.showDxgiDiagnostics` at runtime.
  - Persists to `%LOCALAPPDATA%\ColonyGame\settings.json`.

- **Title-bar DXGI line** (when enabled)
  - `b#`  : swapchain buffer count
  - `tear`: whether tearing is enabled (support + swapchain flag)
  - `si`  : sync interval (0/1)
  - `pf0x`: present flags (hex, e.g. `0x200` = `DXGI_PRESENT_ALLOW_TEARING`)
  - `lat` : max frame latency
  - `wf`  : whether the swapchain was created with the waitable-object flag

## Files touched

- `src/DxDevice.h/.cpp`
- `src/UserSettings.h/.cpp`
- `src/AppWindow_Create.cpp`
- `src/AppWindow_WndProc_Input.cpp`
- `src/AppWindow.h`


# Patch 12: Modifier key precision + Alt-menu suppression + WM_INPUT cleanup

This patch tightens Win32 input correctness and makes left/right modifier bindings truly work.

## What this patch fixes / improves

- `src/AppWindow_WndProc_Input.cpp`
  - **Left/Right modifiers:** when you press **Shift/Ctrl/Alt**, we now emit *both* the generic VK code (e.g. `VK_SHIFT`) **and** the left/right-specific VK code (`VK_LSHIFT`/`VK_RSHIFT`, etc.).
    - Existing bindings using `Shift`/`Ctrl`/`Alt` keep working.
    - New/existing bindings using `LShift`/`RShift`, `LCtrl`/`RCtrl`, `LAlt`/`RAlt` now work as expected.
  - **F10 reliability:** handles `VK_F10` in `WM_SYSKEYDOWN` (Win32 treats F10 as a system key) so the frame-stats title toggle works consistently.
  - **Raw input cleanup:** after processing `WM_INPUT`, we also call `DefWindowProcW` (per Win32 guidance) so the system can do internal cleanup, while still returning 0 to indicate we've processed the message.
  - **Micro-optimization:** caches cursor handles used by `WM_SETCURSOR` to avoid repeated `LoadCursor` calls.

- `src/AppWindow_WndProc_Window.cpp`
  - Handles `WM_SYSCOMMAND` + `SC_KEYMENU` to fully suppress the ALT application menu behavior (so Alt can be used as an in-game modifier without activating the window menu).
