# HPAStar (Header-only demo)

This folder contains a compact, dependency-free **Hierarchical A\*** (HPA\*) implementation intended for
grid-based colony/settlement games.

The actual implementation lives in:

- `HPAStar.hpp`

This Markdown file exists so the header can stay lean (faster to open, less noise in diffs), while still
keeping the original design notes and improvement ideas in-repo.

---

## What is HPA*?

HPA\* (Hierarchical Path-Finding A\*) speeds up long-distance pathfinding on large grids by:

1. **Clustering** the grid into fixed-size regions (clusters).
2. Finding **portals/entrances** along cluster borders where movement is possible.
3. Building an **abstract graph** whose nodes are portals and whose edges represent traversals
   through/between clusters.
4. Running **A\*** on that abstract graph for the high-level route.
5. Running **local A\*** (or another short-range planner) to **refine** each abstract step into real grid tiles.

This reduces search space dramatically on big maps, especially when many agents do long trips.

---

## Design notes (from the original header)

### Why HPA*?
Hierarchical A\* divides the world into clusters and searches over an abstraction.
This reduces search space dramatically for long-distance queries — ideal for colony sims where agents
travel long distances across structured environments (rooms, corridors, streets).

### Dynamic obstacles
The current demo uses a simple strategy: **rebuild abstraction aggressively** when tiles change.
For production, you’ll want a smarter `invalidate_cluster()` that:

- recomputes only the affected portals,
- refreshes intra-cluster caches (e.g., Dijkstra) only for the changed cluster,
- leaves untouched clusters alone.

### Threading
Even if queries remain single-threaded, rebuild work can be parallelized:

- cluster rebuilds can be done per-cluster in parallel
- multiple path queries can run in parallel if the abstract graph is treated as read-only

### Determinism
The search order is deterministic for a given map and heuristic.
If you add random tie-breakers, seed them explicitly (fixed seed in release; configurable seed in debug).

### API surface
Keep the public API small: `HPAStar`, `GridMap`, `Vec2i`, `PathResult`.

### Weighted terrains
Replace `step_cost/diag_cost` with per-tile costs, and accumulate them in `astar_grid()`.

### Serialization
The header contains a small text serializer for quick experiments.
For production, prefer a binary chunk format with versioning.

---

## Practical tips

- Render portals/abstract nodes in debug builds to validate connectivity.
- Typical `cluster_size` values for large maps: **16** or **32** (trade-off between build time and abstraction quality).
- Use **Octile** distance when diagonals are allowed; **Manhattan** when not.
- Batch obstacle edits (update many tiles, then rebuild once) to avoid repeated work.
- If you want to forbid diagonal corner cutting, set `neighbors.corner_cut = false`.

---

## Known limitations of the demo

This header is intentionally minimal and demo-oriented:

- temporary start/goal nodes are not truly removed (indices remain stable)
- portal detection is simplistic (one node per walkable border tile)
- abstraction edges are naive (no cached intra-cluster shortest paths)

These are good “next steps” if you decide to adopt HPA\* for real gameplay AI.
