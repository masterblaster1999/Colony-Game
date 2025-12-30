# Colony-Game Patch (Round 34)

This round focuses on **Pathfinding + movement** improvements:

- Adds a selectable pathfinding algorithm (**AStar** or **JPS**) for direct orders/repathing.
- Adds an optional **LRU path cache** to reduce repeated A* calls.
- Introduces optional **terrain traversal costs** that affect both path costs and actual walk speed.
- Fixes a subtle movement integration issue where colonists could "double-spend" their movement budget when snapping across multiple path nodes in a single tick.

It also bumps the proto-world save format to **v11**.

## New gameplay/simulation

### 1) Pathfinding algorithm toggle (AStar vs JPS)

Under **Tuning / Pathfinding**:

- **Algorithm = AStar** (baseline)
- **Algorithm = JPS** (Jump Point Search; expanded back into tile-by-tile steps for safe movement)

### 2) LRU path cache (validated)

The world now maintains an optional LRU cache for direct `(start, goal)` pathfinding queries.

- Cached paths are **validated** against the current nav grid before reuse.
- Invalid paths are discarded and recomputed.

This mainly helps with repeated manual orders and repathing spikes.

### 3) Terrain traversal costs (optional)

When enabled, some built tiles carry a higher traversal cost:

- **Farm**: slower
- **Stockpile**: slightly slower
- **Door**: tiny penalty

These costs influence:

- Path selection (higher-cost terrain is avoided when reasonable)
- Actual walk speed while crossing those tiles

## UI improvements

### 1) Pathfinding controls + stats

Added a new **Tuning / Pathfinding** section:

- Algorithm selector
- Cache enable + max entries
- Terrain costs enable
- Buttons to clear cache + reset stats
- Live stats: requests/hits, hit rate, computed paths (AStar/JPS), invalidations, evictions

## Save format + persistence

### Save format bump to v11

- **Version bump:** `kWorldVersion` is now **11**
- Added persisted tuning fields:
  - `pathfindingAlgorithm`
  - `pathCacheEnabled`
  - `pathCacheMaxEntries`
  - `navTerrainCostsEnabled`

---

# Colony-Game Patch (Round 33)

This round adds **Doors** plus a first-pass **Rooms / Indoors** system, and fixes a long-standing issue where **Demolish (Remove) plans** could accidentally turn into a permanent built tile.

It also bumps the proto-world save format to **v10**.

## New gameplay/simulation

### 1) Doors (new TileType + tool)

- New buildable tile: **Door**
- Doors are **walkable** (colonists can path through them)
- Doors act as a **room boundary** for indoors detection (they separate rooms)

Hotkey:
- **0** = Door tool

### 2) Demolish plans now truly deconstruct

Previously, completing a **Demolish** plan could incorrectly apply `TileType::Remove` as the *built* tile (and wood refunds would never trigger).

Now:
- Demolish resolves to **Empty built tiles**
- **Plan-built** structures return their **wood cost** when demolished
- Chopping/removing a **Tree** drops **wood yield**
- If a tile becomes **non-walkable**, any loose wood on it is pushed out so it can’t get trapped

### 3) Rooms / Indoors cache

The world now computes a lightweight room graph:

- A “room” is a connected component of **open tiles** (Empty/Floor/Farm/Stockpile)
- Open areas are separated by **boundaries** (Wall/Tree/Door + map edge)
- A room is marked **Indoors** if it is **not connected to the map edge**

The cache rebuilds automatically when room topology changes (e.g., building/removing walls/doors, tree spread).

## UI improvements

### 1) Rooms overlay + room IDs

In **View / Debug**:
- **Show rooms overlay**: shades indoor tiles by room
- **Show room IDs**: labels indoor rooms with “R#” in the world view

### 2) Tile inspection shows room info

The selection panel now shows:
- Whether the tile is in a room-space tile
- Room ID + Indoors/Outdoors + area (tile count)

### 3) Stats show door count

The main colony panel now includes a **Doors** count.

## World generation change

- Removed the old “border walls for orientation” from fresh `reset()` worlds so the Outdoors/Indoors heuristic works as intended (the map edge represents “outside”).

## Save format + persistence

### Save format bump to v10
- **Version bump:** `kWorldVersion` is now **10**
- Tile enums now include **Door**; persistence clamps were updated so Door tiles round-trip correctly.

---

# Colony-Game Patch (Round 32)

This round adds a **Work Priorities** layer (Build / Farm / Haul) and tightens up the **hauling + save-system** so the colony behaves more predictably over longer sessions.

It also bumps the proto-world save format to **v9**.

## New gameplay/simulation

### 1) Per-colonist Work Priorities (Build / Farm / Haul)

Each colonist now has editable work priorities:

- **0 = Off** (never auto-take that work type)
- **1 = Highest**
- **4 = Lowest**

The job assignment logic now checks these priorities when deciding which autonomous jobs a colonist should take.

Notes:
- If priorities tie, the engine's existing assignment order still breaks ties.
- When the colony inventory has **0 food**, farmers will still bootstrap food production (ignoring priorities) so new worlds don't deadlock.

### 2) Hauling job cancellation is now safe

Canceling or preempting hauling jobs (drafting, hunger preemption, manual stop, etc.) now:

- Releases the **pickup reservation** correctly
- Drops any **carried wood** back onto the ground near the colonist so it can be re-hauled
- Clears hauling state cleanly

This fixes cases where wood stacks could remain permanently reserved and/or wood could vanish when a hauling job was interrupted.

## UI improvements

### 1) Colony panel: Work priority controls

The Colonists table now includes three extra columns:

- **B** = Build priority
- **F** = Farm priority
- **H** = Haul priority

There is also a quick button to reset all colonists' work priorities to role-based defaults.

Warnings were expanded to distinguish:
- "No one can do this work" vs
- "This work is disabled by priorities"

### 2) Better job labeling / visuals

- UI job text now recognizes **Hauling**
- World view colors hauling colonists distinctly

## Save format + persistence

### 1) Save format bump to v9
- **Version bump:** `kWorldVersion` is now **9**
- New per-colonist JSON fields:
  - `workPriorities.build`
  - `workPriorities.farm`
  - `workPriorities.haul`

Older saves still load: missing work priorities default from role/capabilities.

### 2) Async snapshot writer fixed
The async save snapshot JSON writer now includes:
- `drafted`
- `role`, `roleLevel`, `roleXp`
- `workPriorities`

This brings snapshot saves back in sync with the authoritative world-save format.

## Tests

- Updated the proto-world save-format test to assert:
  - v9 version value
  - work priorities exist in JSON and round-trip correctly

