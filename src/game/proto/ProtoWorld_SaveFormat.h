#pragma once

// Central place for ProtoWorld save-format identifiers.
//
// Keeping these in one header prevents subtle mismatches between:
//   - colony::proto::World::SaveJson / LoadJson
//   - PrototypeGame's async save worker (PrototypeGame_SaveLoad.cpp)
//   - any future tools that read/write ProtoWorld saves.
//
// NOTE: LoadJson intentionally accepts a legacy format string used by early
//       async-save code paths so older saves remain loadable.

namespace colony::proto::savefmt {

inline constexpr const char* kWorldFormat       = "ColonyGame.ProtoWorld";
inline constexpr const char* kWorldFormatLegacy = "colony_proto_world"; // legacy (pre-save-format-unification)
// Version history (ProtoWorld JSON)
//  v1: initial grid + inventory
//  v2: plan priority
//  v3: hunger/personalFood tuning + per-colonist personalFood
//  v4: builtFromPlan flag (distinguish seeded vs player-built)
//  v5: farmGrowth per cell (grow/harvest farming model)
//  v6: Trees + forestry tuning (tree chopping yield + regrowth)
//  v7: Colonist roles + XP/level progression
//  v8: Loose wood items + hauling tuning (stockpiles become a logistics hub)
inline constexpr int         kWorldVersion      = 8;

} // namespace colony::proto::savefmt
