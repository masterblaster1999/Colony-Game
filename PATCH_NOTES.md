# Patch 7: QoL polish (hotkey repeat fix + settings auto-save + per-user binds)

This patch focuses on "small but high-impact" prototype polish.

## Fixes

- **Hotkey repeat spam**: Holding **V**, **F11**, or **Alt+Enter** no longer repeatedly toggles vsync/fullscreen due to Win32 key auto-repeat.
  - Uses the Win32 `WM_KEYDOWN`/`WM_SYSKEYDOWN` previous key state flag (bit 30) to detect repeats.

## Improvements

- **Debounced settings auto-save**: `settings.json` now auto-saves shortly after changes (vsync toggle, fullscreen toggle, window resize).
  - Debounced to avoid hammering disk writes during live-resize.
  - Still saves on clean exit as before.

- **Per-user input bindings override**:
  - You can now drop `input_bindings.json` or `input_bindings.ini` into:
    - `%LOCALAPPDATA%\ColonyGame\input_bindings.json`
    - `%LOCALAPPDATA%\ColonyGame\input_bindings.ini`
  - These take precedence over `assets/config/...` and repo-root binding files.

## Dev quality-of-life

- Adds `.clang-format`, `.clang-tidy`, and `.editorconfig` to make formatting/linting consistent (especially for CI).
- Improves `tools/format-all.ps1` to auto-discover `clang-format` (PATH or common LLVM install paths).

---

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
