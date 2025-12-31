# Colony-Game Patch (Round 47)

## Rooms Inspector + selected-room overlay

This round expands the room system + debug tooling:

- **Room stats**: rooms now track:
  - **perimeter** (tile-edge boundary length)
  - **door count** (unique adjacent door tiles along the perimeter)
- **Correct cache invalidation**: room caches are marked dirty not only when tiles transition between room-space and boundaries, but also when **doors are added/removed** (stats depend on boundary type).
- **Panels → Rooms**:
  - overlay controls (indoors-only toggle)
  - room ID labels (indoors-only toggle)
  - selected-room outline toggle
  - selected room info + **Focus camera** button
  - **Room Inspector** table (filter indoors/outdoors, click to select, “Go” to focus)
- **World interaction**:
  - **Alt+left-click (Inspect)** selects the room under the cursor (also updates status text).
  - selected room draws with stronger overlay tint + optional outline border.
  - minimap shows selected room bounding box.

### Tests

- Added a doctest verifying **perimeter** and **door count** for a simple enclosed room.

---

# Colony-Game Patch (Round 46)

## Save Browser 2.0: Named saves + copy/rename workflow

- **Named saves**: create arbitrary save files (not limited to slots) directly from the Save Browser.
- **Directory scan**: the browser now lists *all* `*.json` saves in the save folder (excluding `*.meta.json` sidecars), in addition to the standard Slot/Autosave entries.
- **Filter + sort**: quick filter box, category toggles (Slots/Autosaves/Named), and sorting by Kind / Time / Name.
- **Copy / promote tools**:
  - Copy any selected save to a target slot (handy for promoting an autosave).
  - Copy any selected save to a new named save (quick backups / branching).
- **Rename (named saves only)**: rename/move the save (and its meta file when present) with optional overwrite.
- **Safer delete UX**: delete still uses a short confirmation window and now clears selection & refreshes cleanly.
- **Toasts**: copy/rename/delete operations post notifications (and warnings on partial failures).

## Platform utilities

- Added `winpath::copy_file_with_retry()` to match the existing remove/rename retry helpers (plus a unit test).

---

# Colony-Game Patch (Round 45)

This round adds a **Notification / Alerts system** (quality-of-life + playability):

- Added a lightweight **notification log** that records important events (bounded history).
- Added expiring **toast notifications** drawn in the world HUD (top-left).
- Added an **Alerts** section in the Panels window for:
  - enabling/disabling alerts
  - toggling toast overlay
  - tuning alert thresholds + polling interval
  - clearing the message log / toasts
  - viewing a scrollable message history (with optional **Focus** button on targeted alerts)

### Implemented prototype alerts

- **Low wood** and **Low food** threshold warnings (with optional “resolved” messages).
- **No stockpiles** while loose wood exists (hauling cannot happen).
- **No builders / farmers / haulers** available (capabilities or priorities all Off) while work exists.
- **Critical starvation**: colonists below a personal-food threshold while colony food is **0**
  - can optionally auto-pause the simulation.

### Tests

- Added doctest coverage for the new bounded notification log + toast expiry behavior.

## Code changes (Round 45)

- `src/game/util/NotificationLog.h`
  - New small, dependency-free notification log + toast queue.
- `src/game/PrototypeGame_Impl.h`
  - Added notification state + live-tunable alert parameters.
  - Added helper methods: `pushNotification*`, `logMessage`, `focusNotificationTarget`, `updateAlerts`.
- `src/game/PrototypeGame_Sim.cpp`
  - Alert evaluation/polling (`updateAlerts`) + toast TTL ticking.
- `src/game/PrototypeGame_SaveLoad.cpp`
  - Save/autosave completions now also enter the notification log (failures toast as Error).
- `src/game/PrototypeGame_UI_Panels.cpp`
  - New “Alerts” panel section (settings + history log + focus buttons).
- `src/game/PrototypeGame_UI_World.cpp`
  - Toast notifications overlay in the world HUD.
- `tests/notification_log_tests.cpp`
  - New unit tests for notifications.

---
