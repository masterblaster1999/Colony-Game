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
inline constexpr int         kWorldVersion      = 2;

} // namespace colony::proto::savefmt
