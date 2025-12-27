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
