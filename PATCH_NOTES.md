# Colony-Game Patch (Round 31)

This round adds a **Colonist Roles + XP/Level progression** layer on top of the existing prototype AI.

The goal is to make colonies feel less "blob-like" by letting colonists specialize, move/work at different speeds, and naturally improve over time.

It also bumps the proto-world save format to **v7** (and fixes a load/version edge-case from the previous bump).

## New gameplay/simulation

### 1) Colonist Roles (capabilities)
Each colonist now has a **Role** (from `game/Role.hpp`) that defines:

- **Capabilities** (what jobs they will auto-take)
- **Move multiplier** (walk speed)
- **Work multiplier** (task speed)

Autonomous job assignment now respects capabilities:

- **Build plans** require `Capability::Building`
- **Harvest jobs** require `Capability::Farming`

Manual orders (drafted colonists) still work as before.

### 2) Role-based speed multipliers
Roles affect how fast colonists perform tasks:

- **Movement**: `colonistWalkSpeed * role.moveMult`
- **Work**: build/harvest/eat speeds are scaled by `role.workMult`

### 3) XP + Leveling
Colonists earn **XP** for completing real work:

- Completing a **plan** (construction/demolition)
- Completing a **harvest**

XP feeds a simple level-up curve (`RoleComponent::kXpPerLevel`).
Each level grants small bonuses on top of the role multipliers:

- **+1% move speed per level**
- **+2% work speed per level**

## UI improvements

### Colony panel
- Colonists table now includes:
  - **Role** (dropdown)
  - **Level + XP** (tooltip shows effective move/work multipliers)
  - **XP0** button to reset a colonist’s XP/level
- Added quick role assignment buttons:
  - **All Workers / All Builders / All Farmers**
- Added warnings if you have pending work but **no capable colonists** (e.g. zero builders).

### World rendering
- When zoomed in, colonists show a small **role+level label** (e.g. `B2`, `F1`).

## Save format (ProtoWorld v7)

- **Version bump:** `kWorldVersion` is now **7**.
- New per-colonist fields:
  - `role` (stored by **name**)
  - `roleLevel`
  - `roleXp`

### Compatibility / bug fix
- Loader now accepts versions **1..kWorldVersion**.
  - This fixes the prior state where saves written with the latest version could be rejected on load.

## Tests

- Updated the proto-world save-format test to assert:
  - v7 version value
  - colonist role/level/xp fields exist and round-trip
