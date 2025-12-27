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


# Patch 13: Raw input disable semantics + title diagnostics accuracy + WorldGen target fix

This patch is focused on small but important correctness and developer-QOL improvements.

## What this patch fixes / improves

- `src/platform/win32/RawMouseInput.cpp`
  - **Correct toggle semantics:** disabling raw input now takes effect immediately from the game's perspective.
    Even if `RIDEV_REMOVE` fails transiently, we treat raw input as disabled and ignore any stray `WM_INPUT`.

- `src/AppWindow_WndProc_Input.cpp`
  - **Consistent F10 behavior:** `VK_F10` handled in `WM_SYSKEYDOWN` now mirrors the `WM_KEYDOWN` path
    (toggles frame stats + resets the rolling stats window), without unexpectedly touching DXGI diagnostics.

- `src/AppWindow_Create.cpp`
  - **More accurate DXGI diagnostics:** the title bar diagnostics now display the **last actual** `Present()`
    sync interval and flags from `DxDevice`, rather than re-deriving them from current toggles.
  - **Input overflow visibility:** if the fixed per-frame input queue ever overflows, the title now shows
    `InDrop <count>` to make the issue obvious during testing.

- `src/common/ThreadPool.hpp`
  - Adds the missing `<stdexcept>` include so the header is self-sufficient when PCH is disabled.

- `src/platform/win/SingleInstanceGuard.h`
  - Makes the guard header self-sufficient (`<stdexcept>`) and fixes mutex lifecycle (no unconditional
    `ReleaseMutex` on a mutex the thread may not own).

- `src/worldgen/CMakeLists.txt`
  - Fixes an incomplete build definition by adding all required `.cpp` sources (including stage implementations)
    so the `ColonyWorldGen` static library can actually link when enabled.
  - Aligns the target to `cxx_std_23` to match the repo’s overall C++23 toolchain.


# Patch 14: Mouse wheel bindings + minimal bindings editor

This patch adds a high-value, low-risk input upgrade: the mouse wheel is now bindable (and the game includes a small in-game editor to tweak binds and save them back to disk).

## What this patch adds

- **Bindable mouse wheel**
  - New bind tokens: `WheelUp` / `WheelDown`.
  - Wheel detents (`WM_MOUSEWHEEL`) are converted into **impulse-style** input codes so they can participate in chords (e.g. `Ctrl+WheelUp`).

- **Rebindable zoom**
  - New actions: `CameraZoomIn` / `CameraZoomOut`.
  - World zoom now uses these actions instead of directly reading `ImGuiIO::MouseWheel`, so you can remap zoom without touching code.

- **Minimal Bindings Editor UI**
  - Open from **Colony** panel → **Input Bindings** → **Bindings Editor...**
  - Edit per-action binds as comma-separated chords, apply instantly, and save to your current bindings file.

## Small but important behavior improvement

- **Explicit "clear" support in config files**
  - JSON: `"SomeAction": []` clears that action’s binds.
  - INI: `SomeAction =` clears that action’s binds.


# Patch 15: Rectangle paint + plan cache + UI split

This patch improves *building workflow* in the World view and makes the prototype scale a bit better as the number of planned tiles increases.

## What this patch adds

- **Rectangle paint**
  - Hold **Shift + Left-drag** in the World view to place a rectangle of the current tool.
  - Hold **Shift + Right-drag** to erase a rectangle of plans.

## What this patch fixes / improves

- `src/game/proto/ProtoWorld.cpp/.h`
  - **Plan tracking cache:** active planned cells are tracked in an indexed list.
    - `plannedCount()` becomes O(1).
    - Job assignment scans planned cells instead of the entire grid.
  - **Erase semantics fix:** calling `placePlan(..., TileType::Empty)` now truly means **clear an existing plan**.
    - Prevents “empty plans” from being created on already-built tiles when erasing.

- `src/game/PrototypeGame_UI*.cpp`
  - Split the formerly large UI translation unit into smaller files for easier maintenance.

## Patch 16 — Prototype Save/Load + Modifier Bind Fix

- **New:** Proto world persistence:
  - Save world: **Ctrl+S** or **F6** (also available as a button in the Panels UI)
  - Load world: **Ctrl+L** or **F7**
  - Saves to the user's **Saved Games\ColonyGame\proto_world.json** (falls back to LocalAppData if needed).
- **Fix:** Input bindings now treat **Ctrl/Shift/Alt** as generic modifiers (Left/Right variants both satisfy bindings like `Ctrl+S` and `Shift+W`).
- **UI:** Help window updated with the new shortcuts.
- **Build hygiene:** Switched a few Win32-facing headers to include the shared `WinCommon.h` wrapper (reduces macro redefinition warnings).

## Patch 17 — Command-line Safe Mode + Startup Overrides

This patch adds a small, Windows-only command-line parser so you can recover from bad settings or ImGui layout without manually hunting down files.

### New command-line options

- `--safe-mode`
  - Runs with defaults by **ignoring** both `settings.json` and `imgui.ini` for that run.
  - Disables settings writes (no autosave, no shutdown save) so the recovery run is non-destructive.
- `--reset-settings`
  - Deletes `%LOCALAPPDATA%\ColonyGame\settings.json` before launch.
- `--reset-imgui`
  - Deletes `%LOCALAPPDATA%\ColonyGame\imgui.ini` before launch.
- `--ignore-settings`
  - Launch with defaults without deleting `settings.json` (the file is not read).
- `--ignore-imgui-ini`
  - Launch without reading/writing `imgui.ini` (default UI layout each run).

### New startup overrides (useful for testing)

- `--fullscreen` / `--windowed`
- `--vsync` / `--novsync`
- `--rawmouse` / `--norawmouse`
- `--width <px>` / `--height <px>`
- `--max-frame-latency <1..16>`
- `--maxfps <0|N>` (VSync off cap)
- `--bgfps <0|N>` (background cap)

### Implementation notes

- `AppWindow` now has a `CreateOptions` struct so startup settings can be overridden in a single place.
- The ImGui layer can be initialized with ini persistence disabled (prevents loading an off-screen layout).

## Patch 18 — Plan Undo/Redo + Editor QoL

This patch improves the in-game building workflow by making plan placement reversible and easier to debug.

### New

- **Undo/Redo for plan placement**
  - Undo: **Ctrl+Z**
  - Redo: **Ctrl+Y** or **Ctrl+Shift+Z**
  - Drag strokes and rectangle placements are grouped into a single command.
  - "Clear Plans" is now undoable (one command).

- **New bindable actions:** `Undo`, `Redo`
  - Added to the default binding files:
    - `assets/config/input_bindings.json`
    - `assets/config/input_bindings.ini`

- **View/Debug toggles (Colony panel)**
  - Brush preview overlay
  - Colonist path drawing
  - Reservation ID overlay

- **World Reset + tuning panel**
  - Reset width/height and seed controls
  - Live tuning sliders (build speed, walk speed, farm/food rates)

### Implementation notes

- `src/game/editor/PlanHistory.*` holds the command stack and applies changes through `World::placePlan()` to keep caches consistent.
- Added `World::CancelAllJobsAndClearReservations()` so undo/redo (and bulk edits) don't leave colonists walking toward stale targets.


## Patch 19 — Job assignment performance + rectangle paint reliability (2025-12-27)

### What’s new
- **Faster job assignment**: idle colonists now do a single *multi-goal* path search to the nearest available plan, instead of running A* once per plan tile. This avoids big CPU spikes when many plans exist.
- **Job assignment throttle**: assignment attempts are throttled (~5×/sec) while idle colonists exist, preventing pathological “try/fail every tick” loops.
- **Rectangle paint reliability**: `Shift + drag` rectangle plans now apply even if you release the mouse outside the world canvas.
- **Tiny-world guard**: world reset no longer creates invalid RNG ranges when `width <= 2` or `height <= 2` (useful for edge-case saves/tests).

### Files changed
- `src/game/proto/ProtoWorld.h`
- `src/game/proto/ProtoWorld.cpp`
- `src/game/PrototypeGame_UI_World.cpp`


## Patch 20 — Plan priorities + save slots + autosave (2025-12-27)

### What’s new
- **Plan priorities (P1..P4)**
  - Each planned tile now stores a small priority value.
  - Colonists prefer building **higher priority plans first**, then nearest.
  - UI: new **Brush Priority** control in the Colony panel.
  - Default hotkeys (bindable): **PageUp/PageDown**.
  - View/Debug: optional **Show plan priorities** overlay.

- **Selection / Inspect upgrades**
  - Inspect tool click now selects a tile.
  - The Colony panel shows a **Selection** section.
  - If the selected tile has an active plan, you can edit its priority (undoable).

- **Save slots + autosave controls**
  - Added manual **Save Slot / Load Slot** with slot selector (0..9).
  - Added **Autosave** (enable/interval/keep count) with rotating autosave files.
  - UI buttons for **Autosave Now** and **Load Autosave (Newest)**.

### Compatibility
- Save format bumped to v2 to store plan priority, but **v1 saves still load** (priority defaults to P1).

### Files changed
- `src/game/proto/ProtoWorld.h`
- `src/game/proto/ProtoWorld.cpp`
- `src/game/editor/PlanHistory.h`
- `src/game/editor/PlanHistory.cpp`
- `src/input/InputMapper.h`
- `src/input/InputMapper.cpp`
- `src/game/PrototypeGame_Impl.h`
- `src/game/PrototypeGame_Input.cpp`
- `src/game/PrototypeGame_Sim.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/PrototypeGame_UI_World.cpp`
- `src/game/PrototypeGame_UI_Panels.cpp`
- `src/game/PrototypeGame_UI_Help.cpp`
- `assets/config/input_bindings.json`
- `assets/config/input_bindings.ini`


## Patch 21 — Priority paint tool + async saves + built-count cache (2025-12-27)

### What’s new

Gameplay / UX
- **New tool: Priority (`7`)**
  - Paints the current **Brush Priority** onto **existing plans** (does not place new plans / does not change plan type).
  - Works with rectangle paint (**Shift + drag**).
  - Fully undo/redo-able.

Performance / Reliability
- **Async saves**: manual saves and autosaves now run on a background worker thread.
  - Reduces frame hitches, especially with autosave enabled.
  - Autosaves are **generation-guarded** so an old queued autosave can’t overwrite the newest autosave after a load/reset.
- **Built tile count cache**: `builtCount(...)` is now O(1) via a cached counter array instead of scanning the whole grid every tick.

Save format
- Save files now include an optional `meta` object (ignored by the loader) with:
  - `kind`: `"manual"` / `"autosave"`
  - `savedUnixSecondsUtc`
  - `playtimeSeconds`

### Files changed
- `src/game/proto/ProtoWorld.h`
- `src/game/proto/ProtoWorld.cpp`
- `src/game/PrototypeGame_Impl.h`
- `src/game/PrototypeGame_Input.cpp`
- `src/game/PrototypeGame_Sim.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/PrototypeGame_UI_World.cpp`
- `src/game/PrototypeGame_UI_Panels.cpp`
- `src/game/PrototypeGame_UI_Help.cpp`


## Patch 22 — Save browser + sidecar save metadata + job-search perf (2025-12-27)

### What’s new

Save / Load UX
- **Save Browser (UI)**
  - New **Save Browser** section in the panels window.
  - Lists **save slots** and **autosaves** with quick info (timestamp, playtime, world size, population, inventory, counts).
  - One-click **Load**, **Show in Explorer**, and **Delete** (with a short confirm timer).
  - **Open Save Folder** button.

Performance / Reliability
- **Sidecar save metadata (`*.meta.json`)**
  - Manual saves and autosaves now write a small sidecar metadata file next to the world save:
    - Example: `proto_world_slot_3.json` -> `proto_world_slot_3.meta.json`
  - This prevents the UI from needing to parse the full (potentially huge) world JSON just to show save previews.
  - Autosave rotation now rotates the matching `.meta.json` files too.
- **Job search optimization**
  - Nearest-plan search now reuses scratch buffers (generation-stamped) instead of allocating and clearing large arrays every query.
  - Removed a redundant plan-priority filter check in the adjacency test.

### Files changed
- `src/CMakeLists.txt`
- `src/game/save/SaveMeta.h`
- `src/game/save/SaveMeta.cpp`
- `src/game/PrototypeGame_Impl.h`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/PrototypeGame_UI_Panels.cpp`
- `src/game/proto/ProtoWorld.h`
- `src/game/proto/ProtoWorld.cpp`


## Patch 23 — Save thumbnails (meta) + Save Browser preview (2025-12-27)

### What’s new

Save / Load UX
- **Save Browser previews**
  - Sidecar save metadata files (`*.meta.json`) now include a tiny **thumbnail** of the world.
  - The Save Browser displays this thumbnail so you can visually pick a save without loading it.
  - Thumbnail is sampled from the world grid and stored compactly as **base64**.
  - Old meta files won’t have thumbnails until you **make a new save / autosave**.

### Files changed
- `src/CMakeLists.txt`
- `src/game/save/Base64.h`
- `src/game/save/Base64.cpp`
- `src/game/save/SaveMeta.h`
- `src/game/save/SaveMeta.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/PrototypeGame_UI_Panels.cpp`
