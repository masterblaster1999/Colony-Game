#pragma once

#include "CGCombatTypes.hpp"
#include "CGDamageModel.hpp"
#include "CGStatusEffects.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace colony::combat {

enum class CombatEventType : std::uint8_t {
  AttackRequested = 0,
  AttackMissed    = 1,
  AttackHit       = 2,
  DamageApplied   = 3,
  StatusApplied   = 4,
  UnitDied        = 5,
  ReloadStarted   = 6,
  ReloadFinished  = 7
};

[[nodiscard]] constexpr std::string_view to_string(CombatEventType t) noexcept {
  switch (t) {
    case CombatEventType::AttackRequested: return "AttackRequested";
    case CombatEventType::AttackMissed:    return "AttackMissed";
    case CombatEventType::AttackHit:       return "AttackHit";
    case CombatEventType::DamageApplied:   return "DamageApplied";
    case CombatEventType::StatusApplied:   return "StatusApplied";
    case CombatEventType::UnitDied:        return "UnitDied";
    case CombatEventType::ReloadStarted:   return "ReloadStarted";
    case CombatEventType::ReloadFinished:  return "ReloadFinished";
    default:                               return "Unknown";
  }
}

struct CombatEvent {
  CombatEventType type{CombatEventType::AttackRequested};
  EntityId source{kInvalidEntity};
  EntityId target{kInvalidEntity};

  // For damage events
  DamagePacket damage{};
  float total_damage{0.0f};
  bool critical{false};

  // For status events
  StatusEffect status{StatusEffect::Count};
  std::uint16_t stacks{0};
};

// Compact human-readable summary (for debug overlays / logs).
[[nodiscard]] inline std::string describe_event(const CombatEvent& e) {
  std::string out;
  out.reserve(128);

  out.append(std::string(to_string(e.type)));
  out.append(" src=");
  out.append(std::to_string(e.source));
  out.append(" tgt=");
  out.append(std::to_string(e.target));

  if (e.type == CombatEventType::AttackHit || e.type == CombatEventType::DamageApplied) {
    out.append(" dmg=");
    out.append(std::to_string(e.total_damage));
    if (e.critical) out.append(" (CRIT)");
  }
  if (e.type == CombatEventType::StatusApplied) {
    out.append(" status=");
    out.append(std::string(to_string(e.status)));
    out.append(" stacks=");
    out.append(std::to_string(e.stacks));
  }

  return out;
}

} // namespace colony::combat
