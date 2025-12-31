# Colony-Game Patch (Round 36)

This round adds a **manual order queue** for **drafted colonists** (combat-style command queue):

- **Shift + Right-click** (Inspect tool) now **queues** manual orders (Move / Build / Harvest).
- Queued orders execute **in sequence**, and the **front order persists** through interruptions
  (e.g. hunger/eating) until it completes.
- UI improvements:
  - Colonist table shows **Q#** when a manual queue exists, plus a **ClrQ** button.
  - Selected-colonist **Manual Orders** tree lets you **reorder / delete / clear** queued orders.
  - The world view draws **queue markers** (numbered dots) for the selected colonist.

## How to use

1) Select a colonist (Inspect tool → **Left-click** the colonist).
2) **Draft** the colonist.
3) Issue orders:
   - **Right-click**: replaces the queue with a single order (immediate).
   - **Shift + Right-click**: **adds** an order to the end of the queue.

Notes:
- Build orders only queue on **planned** tiles.
- Harvest orders only queue on **ripe farms** (growth ≥ 1.0), matching the previous behavior.

## Save compatibility

- Save format version bumped to **v12** to persist the manual order queue.
- Older saves still load (queue defaults empty).

## Code changes (Round 36)

- `src/game/proto/ProtoWorld.h/.cpp`
  - Added `Colonist::ManualOrder` + `manualQueue`
  - Added queue execution + completion popping
  - Added Shift-queue support to `OrderColonistMove/Build/Harvest`
- `src/game/proto/ProtoWorld_Persistence.cpp`
  - Save/load `manualQueue`
- `src/game/proto/ProtoWorld_SaveFormat.h`
  - Save version → v12
- `src/game/PrototypeGame_UI_World.cpp`
  - Shift+Right-click queuing
  - World overlay for queued orders
- `src/game/PrototypeGame_UI_Panels.cpp`
  - Queue indicators + queue management UI

---

# Colony-Game Patch (Round 35)

This round focuses on **Blueprint tooling** improvements (planning/QoL):

- Adds **Blueprint transforms**: rotate (CW / CCW / 180) and flip (horizontal / vertical).
- Adds a **Blueprint Library (Disk)** panel:
  - Save the current blueprint to disk under the game's Saved Games folder.
  - Browse saved blueprints (newest-first), preview them, load into the active blueprint, and delete.
  - Quick button to open the blueprint folder in Explorer.

## New editor tools / UI

### 1) Blueprint transforms

In **Panels → Blueprints**, you now have transform buttons:

- **Rotate CW / CCW / 180**
- **Flip Horizontal / Vertical**

These operate directly on the current blueprint (including priorities) and preserve the packed plan data.

### 2) Blueprint library (disk)

In **Panels → Blueprints → Blueprint Library (Disk)**:

- Choose a **Save name** and click **Save current** to write a blueprint file.
- Use **Refresh** to rescan the folder.
- Select a file to see a **preview** and then:
  - **Load selected → current blueprint**
  - **Delete selected**

Blueprints are stored at:

- `<Saved Games>/<Product>/blueprints`
  - (The exact Saved Games base is resolved by PathUtilWin and will fall back to LocalAppData if needed.)

## Implementation notes

- Blueprint files use the **same JSON schema** as the clipboard format (`PlanBlueprintToJson` / `PlanBlueprintFromJson`).
- Saving uses an **atomic write** to avoid partial/corrupted files.

## Code changes (Round 35)

- `src/game/editor/Blueprint.*`
  - Added transform helpers (rotate/flip).
- `src/game/editor/BlueprintLibrary.*`
  - New helper module for saving/loading/listing blueprint files on disk.
- `src/game/PrototypeGame_UI_Panels.cpp`
  - Added transform buttons and the Blueprint Library UI.
- `src/game/PrototypeGame_SaveLoad.cpp`
  - Added `blueprintDir()` path helper for the blueprint library.
