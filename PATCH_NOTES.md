# Colony-Game Patch (Round 13)

This patch improves **pathfinding API correctness** by honoring output-shape options in `JpsOptions`:

- **Fix:** `jps_find_path_impl()` now respects `returnDensePath` and `preferJumpPoints` (previously the adapter always densified the path).
- **Tests:** added doctest coverage validating dense (step-by-step) output vs sparse “jump point” output.

## Files
- `pathfinding/JpsAdapter.cpp`
- `tests/test_jps.cpp`
- `PATCH_NOTES.md`

---

# Colony-Game Patch (Round 12)

This patch focuses on **undo/redo stability + test coverage** for the prototype plan editor:

- **Fix:** `PlanHistory::Redo()` now enforces the configured **max history cap** even if the cap is lowered after an undo (previously redo could grow the undo stack beyond the new cap).
- **Tests:** added a dedicated doctest suite for PlanHistory (undo/redo round-trip, duplicate tile edit merge, and history-cap trimming behavior).
- **Build/Test plumbing:** the test target now compiles the minimal prototype modules it exercises (`ProtoWorld` persistence + `PlanHistory`) directly into `colony_tests`, so tests that call `World::SaveJson/LoadJson()` link reliably.

## Files
- `src/game/editor/PlanHistory.cpp`
- `tests/test_plan_history.cpp`
- `tests/CMakeLists.txt`
- `PATCH_NOTES.md`

---

# Colony-Game Patch (Round 11)

This patch improves **input bindings ergonomics on Windows** by adding a **per-user override location**
under `%LOCALAPPDATA%\ColonyGame`, so players can customize bindings without editing files inside the install directory.

- **Per-user bindings search path**: the game now checks `%LOCALAPPDATA%\ColonyGame\input_bindings.{json|ini}` first,
  then falls back to the existing `assets/config/...` and working-directory candidates.
- **Bindings Editor defaults to the per-user target** (and adds quick target buttons for per-user JSON/INI and the currently loaded file).
- **New CLI recovery flag**: `--reset-bindings` deletes per-user `input_bindings.{json|ini}` overrides (and any exe-dir overrides).

## Files
- `src/game/PrototypeGame_Input.cpp`
- `src/game/PrototypeGame_UI_BindingsEditor.cpp`
- `src/input/InputMapper.cpp`
- `src/input/InputMapper.h`
- `src/app/CommandLineArgs.cpp`
- `src/app/CommandLineArgs.h`
- `src/AppMain.cpp`

---

# Colony-Game Patch (Round 10)

This patch continues the **Windows robustness** theme with two small but high-impact improvements:

- **ImGui layout reset is now resilient**: deleting `imgui.ini` uses `winpath::remove_with_retry()` so transient file locks (Defender, Explorer preview handlers, editors) don’t make the in-game reset action randomly fail.
- **Added CI tests** for the retry helpers (`remove_with_retry` + `rename_with_retry`), including deleting **read-only** files.

## Fixes & improvements

### 1) ImGui “Reset UI layout” delete uses winpath retry helper
The ImGui debug menu includes **Reset UI layout (delete imgui.ini)**. Previously it used `std::filesystem::remove`, which can fail intermittently on Windows when another process briefly holds a handle to the file.

**Change:** use `winpath::remove_with_retry()` and surface the Windows error code in the UI status string when deletion fails.

**Files:**
- `src/ui/ImGuiLayer.cpp`

### 2) Tests for winpath retry ops
Added a small Windows-only regression suite covering:
- `rename_with_retry()` successful rename
- `rename_with_retry()` error reporting when the source is missing
- `remove_with_retry()` removing a **read-only** file (clears the attribute best-effort)

**Files:**
- `tests/test_winpath_retry_ops.cpp`

---

# Colony-Game Patch (Round 9)

This patch continues the **stability + DX polish** work:

- **Config saves are now atomic on Windows** (`core::SaveConfig`) to prevent truncated `config.ini` on crashes or transient locks.
- **Config loads are more resilient**: Windows reads use retry/backoff + size guard, and parsing no longer throws on corrupt values.
- **Added tests** covering config save/load and corrupt-value tolerance.
- **Lint workflow cancels older runs** (concurrency) and uses least-privilege permissions to reduce CI noise and overlap.

## Fixes & improvements

### 1) Atomic + resilient config.ini (core::Config)
`src/core/Config.cpp` was using `std::stoi` and `std::ofstream(..., trunc)` directly. That had two practical issues:

- A malformed `config.ini` could throw (and potentially crash) during startup.
- On Windows, a save could be truncated if the process died mid-write or if a transient lock interfered.

**Improvements:**
- Safe parsing via `std::from_chars` (no exceptions) + whitespace/comment handling.
- Windows loads use `winpath::read_file_to_string_with_retry()` with a 1 MiB guardrail.
- Windows saves use `winpath::atomic_write_file()` and report actionable errors via `LOG_ERROR`.
- Save now ensures the directory exists (`create_directories`).

**Files:**
- `src/core/Config.cpp`

### 2) Tests for config save/load robustness
Added a small regression suite ensuring:
- Save creates the directory and writes `config.ini`
- Load round-trips values
- Corrupt values don’t throw and don’t clobber existing defaults

**Files:**
- `tests/test_core_config.cpp`

### 3) Lint workflow: concurrency + least-privilege
The PR lint workflow now cancels older runs on the same ref/PR and runs with minimal permissions.

**Files:**
- `.github/workflows/lint.yml`

---

# Colony-Game Patch (Round 8)

This patch continues the **Windows robustness** theme, focusing on **user-editable config files**:

- **Settings & input bindings now load with retry/backoff** to tolerate transient file locks from Defender, Explorer preview handlers, or editors.
- **Bindings editor saves atomically** (temp file + replace) to avoid partial/truncated writes and to play nicely with hot-reload.
- **Added test coverage** for the Win32 IO helpers (atomic write + max-bytes guard).
- Minor include hygiene: `test_jps.cpp` now includes the public header via `<pathfinding/Jps.hpp>`.

## Fixes & improvements

### 1) Robust settings.json reads (retry/backoff + size guardrail)
`LoadUserSettings()` now uses `winpath::read_file_to_string_with_retry()` so a brief sharing/lock violation doesn't force the game back to defaults.

**Files:**
- `src/UserSettings.cpp`

### 2) Robust input bindings reads (retry/backoff + size guardrail)
`InputMapper::LoadFromFile()` now uses the robust Windows read helper. This reduces “bindings temporarily reset to defaults” during hot reload if the bindings file is mid-save.

**Files:**
- `src/input/InputMapper.cpp`

### 3) Bindings editor saves atomically (and with retry)
The in-app bindings editor now writes via `winpath::atomic_write_file()` (temp file + replace + retry/backoff).

**Files:**
- `src/game/PrototypeGame_UI_BindingsEditor.cpp`

### 4) Tests for winpath IO helpers
Added small targeted tests for `atomic_write_file()` and `read_file_to_string_with_retry()` (including the `max_bytes` guard).

**Files:**
- `tests/test_atomic_write.cpp`

### 5) Include hygiene
Use angle brackets for the public pathfinding header include.

**Files:**
- `tests/test_jps.cpp`

---

# Colony-Game Patch (Round 7)

This patch focuses on **Windows-only robustness and maintainability**:
- **Robust file reads with retry/backoff** (mirrors the write-side resiliency you already have).
- **More tolerant save loading** (salvages saves with minor JSON type issues instead of hard-failing).
- **Split `ProtoWorld.cpp`** by extracting persistence into its own translation unit to keep the core sim file smaller.

## Fixes & improvements

### 1) Robust file reads on Windows (retry/backoff + size guardrail)
Added `winpath::read_file_to_string_with_retry()` to tolerate transient `ERROR_SHARING_VIOLATION` / `ERROR_LOCK_VIOLATION`
from background scanners and shell handlers. It also supports an explicit `max_bytes` limit.

**Files:**
- `src/platform/win/PathUtilWin.h`
- `src/platform/win/PathUtilWin.cpp`

### 2) Save/load robustness: tolerant JSON numeric parsing
`ProtoWorld` save loading now uses safe numeric extraction for cells/colonists, so minor type mismatches
(e.g. floats where ints were expected) don't crash or hard-fail the load.

**Files:**
- `src/game/proto/ProtoWorld_Persistence.cpp` (new)
- `src/game/proto/ProtoWorld.cpp` (persistence removed)

### 3) Meta reads use the robust Windows read helper
Sidecar `.meta.json` reads now also retry/backoff under transient locks and include `std::error_code` details on failure.

**Files:**
- `src/game/save/SaveMeta.cpp`

### 4) Build integration
Added the new persistence translation unit to the main executable.

**Files:**
- `src/CMakeLists.txt`

---

# Colony-Game Patch (Round 6)

This patch focuses on **bulletproof Windows atomic saves** (no temp-file collisions, retries under transient locks)
and **more actionable save error messages**.

## Fixes & improvements

### 1) Atomic write: unique temp files + retry replace/move (Windows)
`winpath::atomic_write_file()` writes saves/settings via a temporary file and then swaps it into place.
Two subtle Windows-specific issues could still cause intermittent failures:

- **Temp filename collisions** (two threads writing the same target within the same millisecond).
- **Transient replace failures** (Explorer/Defender briefly holding a handle, causing ReplaceFileW/MoveFileExW to fail).

**Fixes:**
- Temp files are now created with a unique suffix (PID + TID + tick + monotonic counter) and `CREATE_NEW`.
- Replace/Move now retries with backoff on transient Win32 sharing/lock errors.
- Temp cleanup uses `remove_with_retry()` to reduce leftover `.tmp` files.
- Added a new overload `atomic_write_file(..., std::error_code* out_ec)` so callers can surface the real Win32 error.

**Files:**
- `src/platform/win/PathUtilWin.h`
- `src/platform/win/PathUtilWin.cpp`

### 2) Save failures now show the real Windows reason
When a save failed, the user-facing message was too generic (`atomic_write_file failed` / `Failed to write save file.`),
which makes debugging permission/lock issues hard.

**Improvement:**
- Save error strings now include the underlying `std::error_code` message + numeric code when available.

**Files:**
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/proto/ProtoWorld.cpp`

### 3) Legacy Windows platform opt-in: avoid duplicate atomic_write_file symbols
If `COLONY_PLATFORM_WIN_INCLUDE_LEGACY` was enabled, the legacy tree could accidentally compile
an extra `winpath_atomic_write.cpp` implementation of `winpath::atomic_write_file()`, creating duplicate symbols.

**Fix:** exclude `winpath_atomic_write.cpp` from the legacy glob.

**Files:**
- `src/platform/win/CMakeLists.txt`

---

# Colony-Game Patch (Round 5)

This patch focuses on **Windows robustness** (transient file locks) and **save browser UX**.

## Fixes & improvements

### 1) Robust Windows file operations (retry on transient locks)
On Windows, deleting/renaming files can intermittently fail due to background scanners (Defender),
Explorer preview handlers, or other processes briefly holding file handles.

- Added `winpath::remove_with_retry()` and `winpath::rename_with_retry()` (retry + backoff, best-effort clears read-only attribute).
- Autosave rotation now uses the retry helpers (less chance of autosave rotation breaking under file locks).
- Save browser delete now uses the retry helpers.
- Reset-settings command line deletes now use the retry helpers.
- Log rotation/pruning now uses the retry helpers.

**Files:**
- `src/platform/win/PathUtilWin.h`
- `src/platform/win/PathUtilWin.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/game/PrototypeGame_UI_Panels.cpp`
- `src/AppMain.cpp`
- `src/platform/win/LauncherLoggingWin.cpp`

### 2) Save Browser: better timestamps + delete orphan meta
Save slots that lacked a `.meta.json` sidecar (older saves, copied files, etc.) showed no timestamp.

- The save list now shows a best-effort timestamp:
  - Prefer meta `savedUnixSecondsUtc`
  - Otherwise fall back to filesystem last-write-time and label it as `(mtime)`
- The details panel shows a `Modified:` line when meta is missing/unreadable.
- Delete now works for “meta-only” orphan entries (deletes the meta file too).

**Files:**
- `src/game/PrototypeGame_Impl.h`
- `src/game/PrototypeGame_UI_Panels.cpp`

### 3) Autosave cancellation: re-check generation after rotation
Autosaves already skip if a load/reset happened *before* the task started, but a load/reset could
still happen while rotating autosaves.

- Added a second autosave generation check after rotation and before writing `autosave_00.json`
  to reduce the chance of an “old world” autosave being written after a load/reset.

**Files:**
- `src/game/PrototypeGame_SaveLoad.cpp`

---

# Colony-Game Patch (Round 4)

This patch focuses on **responsiveness during autosaves** and **better default UI fonts on Windows**.

## Fixes & improvements

### 1) Autosave queue coalescing (prevents backlog)
If autosaves trigger faster than the worker can write them (slow disk, huge worlds, etc.),
the queue could grow and cause extra disk churn.

- AsyncSaveManager now removes any *queued* autosave tasks before enqueueing a new autosave.
- Manual saves are preserved and still run before autosaves.

**Files:**  
- `src/game/PrototypeGame_SaveLoad.cpp`

### 2) Async save worker thread: name + lower priority
To make debugging/profiling easier (Visual Studio, ETW) and reduce the chance of save IO
competing with render/input:

- The async save worker thread is named **AsyncSave** (best effort via runtime-loaded `SetThreadDescription`).
- The worker thread priority is set to `THREAD_PRIORITY_BELOW_NORMAL`.

**Files:**  
- `src/game/PrototypeGame_SaveLoad.cpp`

### 3) ImGui default font fallback to Windows system fonts
When running from a build output folder that doesn't have `res/fonts` copied next to the EXE,
the UI could fall back to ImGui's built-in font.

- The font picker now also checks `%WINDIR%\Fonts` for common fonts (Segoe UI, Arial).

**Files:**  
- `src/ui/ImGuiLayer.cpp`

---

# Colony-Game Patch (Round 3)

This patch focuses on **save/load correctness** and **Windows usability**.

## Fixes & improvements

### 1) Save/Load format mismatch (critical fix)
PrototypeGame's async save worker was writing a world save JSON with:

- `"format": "colony_proto_world"`

…but `colony::proto::World::LoadJson()` validates against:

- `"format": "ColonyGame.ProtoWorld"`

That mismatch made freshly created saves fail to load.

**Fix:** PrototypeGame's async save writer now uses the same format string/version as ProtoWorld.

### 2) Backward compatibility for legacy saves
Even after fixing the writer, you may already have save files created with the legacy
`"colony_proto_world"` format string.

**Improvement:** `World::LoadJson()` now accepts **both**:
- `"ColonyGame.ProtoWorld"` (current)
- `"colony_proto_world"` (legacy)

### 3) Centralized save format constants
To prevent future mismatches, the save format identifiers are now defined once in:

- `src/game/proto/ProtoWorld_SaveFormat.h`

…and reused by:
- `src/game/proto/ProtoWorld.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`

### 4) Windows-style /switch command line support
The command line parser claimed it normalized `/foo` to `--foo`, but it only changed the
leading slash to a single dash, which caused many `/...` switches to be treated as unknown.

**Fix:** `/foo` now normalizes to `--foo` (and common short aliases like `/w`, `/hgt`, `/mfl`,
`/fps`, `/bgfps` normalize to `-w`, `-hgt`, etc.).

## Files in this zip (Round 3)

- `src/game/proto/ProtoWorld_SaveFormat.h`
- `src/game/proto/ProtoWorld.cpp`
- `src/game/PrototypeGame_SaveLoad.cpp`
- `src/app/CommandLineArgs.cpp`
- `PATCH_NOTES.md`
- `tests/test_proto_world_save_format.cpp`

---

# Colony-Game Patch (Round 2)

This patch fixes the remaining MSVC build errors:

- C4150: deletion of pointer to incomplete type 'colony::game::AsyncSaveManager'
- C2338: static assertion failed: "can't delete an incomplete type"
- C2027: use of undefined type 'colony::game::AsyncSaveManager'

## What changed

### 1) Use a custom deleter for AsyncSaveManager
MSVC's std::default_delete<T> has a static_assert that fails if T is incomplete at the point the unique_ptr destructor is instantiated.
To make this robust, we:

- Add `AsyncSaveManagerDeleter` + `AsyncSaveManagerPtr` in `src/game/PrototypeGame_Impl.h`
- Change `PrototypeGame::Impl::saveMgr` to use `AsyncSaveManagerPtr` instead of `std::unique_ptr<AsyncSaveManager>`

### 2) Define the deleter where AsyncSaveManager is complete
`AsyncSaveManager` is defined in `src/game/PrototypeGame_SaveLoad.cpp`, so we define:

`void AsyncSaveManagerDeleter::operator()(AsyncSaveManager*) const noexcept`

in that file, after the `AsyncSaveManager` definition.

### 3) Replace make_unique calls
Because `std::make_unique` returns a `std::unique_ptr<T>` with the default deleter, we replace:

`std::make_unique<AsyncSaveManager>()`

with:

`AsyncSaveManagerPtr{ new AsyncSaveManager() }`

## Files in this zip (Round 2)

- src/game/PrototypeGame_Impl.h
- src/game/PrototypeGame_SaveLoad.cpp
- PATCH_NOTES.md
