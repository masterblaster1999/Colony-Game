#pragma once

#include "CGCombatTypes.hpp"
#include "CGDamageModel.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace colony::combat {

enum class StatusEffect : std::uint8_t {
  Bleeding = 0,
  Burning  = 1,
  Poisoned = 2,
  Stunned  = 3,
  Slowed   = 4,
  Suppressed = 5,
  Count
};

inline constexpr std::size_t kStatusEffectCount =
    static_cast<std::size_t>(StatusEffect::Count);

[[nodiscard]] constexpr std::string_view to_string(StatusEffect s) noexcept {
  switch (s) {
    case StatusEffect::Bleeding:   return "Bleeding";
    case StatusEffect::Burning:    return "Burning";
    case StatusEffect::Poisoned:   return "Poisoned";
    case StatusEffect::Stunned:    return "Stunned";
    case StatusEffect::Slowed:     return "Slowed";
    case StatusEffect::Suppressed: return "Suppressed";
    default:                       return "Unknown";
  }
}

struct StatusSpec {
  float tick_interval_sec{1.0f}; // 0 => no periodic ticking
  DamagePacket dot_per_tick{};   // damage per tick (per stack)
  float accuracy_mult{1.0f};     // multiplicative
  float move_mult{1.0f};         // multiplicative
  bool blocks_attacks{false};
};

[[nodiscard]] inline StatusSpec default_spec(StatusEffect type) noexcept {
  switch (type) {
    case StatusEffect::Bleeding: {
      StatusSpec s{};
      s.tick_interval_sec = 1.0f;
      s.dot_per_tick = DamagePacket::single(DamageType::Kinetic, 1.0f);
      return s;
    }
    case StatusEffect::Burning: {
      StatusSpec s{};
      s.tick_interval_sec = 1.0f;
      s.dot_per_tick = DamagePacket::single(DamageType::Fire, 1.5f);
      s.accuracy_mult = 0.9f;
      return s;
    }
    case StatusEffect::Poisoned: {
      StatusSpec s{};
      s.tick_interval_sec = 1.0f;
      s.dot_per_tick = DamagePacket::single(DamageType::Poison, 1.0f);
      return s;
    }
    case StatusEffect::Stunned: {
      StatusSpec s{};
      s.blocks_attacks = true;
      s.accuracy_mult = 0.0f;
      s.move_mult = 0.0f;
      s.tick_interval_sec = 0.0f;
      return s;
    }
    case StatusEffect::Slowed: {
      StatusSpec s{};
      s.move_mult = 0.6f;
      s.tick_interval_sec = 0.0f;
      return s;
    }
    case StatusEffect::Suppressed: {
      StatusSpec s{};
      s.accuracy_mult = 0.7f;
      s.tick_interval_sec = 0.0f;
      return s;
    }
    default:
      return {};
  }
}

struct StatusInstance {
  StatusEffect type{StatusEffect::Bleeding};
  float remaining_sec{0.0f};
  float tick_accumulator_sec{0.0f}; // time until next tick; counts down
  std::uint16_t stacks{1};
  float magnitude{1.0f}; // optional extra multiplier for certain effects (slow strength etc.)
};

struct StatusAggregate {
  float accuracy_mult{1.0f};
  float move_mult{1.0f};
  bool blocks_attacks{false};
};

// Simple policies:
//  - If same type exists: refresh duration to max(existing, new) and add stacks up to cap.
//  - tick_accumulator is clamped so effects don't "burst" on refresh.
inline void add_or_refresh(
    std::vector<StatusInstance>& statuses,
    StatusEffect type,
    float duration_sec,
    std::uint16_t add_stacks = 1,
    std::uint16_t stack_cap = 5,
    float magnitude = 1.0f)
{
  duration_sec = std::max(0.0f, duration_sec);
  add_stacks = std::max<std::uint16_t>(1, add_stacks);
  stack_cap = std::max<std::uint16_t>(1, stack_cap);

  for (auto& st : statuses) {
    if (st.type != type) continue;
    st.remaining_sec = std::max(st.remaining_sec, duration_sec);
    st.stacks = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        static_cast<std::uint32_t>(st.stacks) + add_stacks,
        static_cast<std::uint32_t>(stack_cap)));
    st.magnitude = std::max(st.magnitude, magnitude);

    const StatusSpec spec = default_spec(type);
    if (spec.tick_interval_sec > 0.0f) {
      st.tick_accumulator_sec = std::clamp(st.tick_accumulator_sec, 0.0f, spec.tick_interval_sec);
    } else {
      st.tick_accumulator_sec = 0.0f;
    }
    return;
  }

  StatusInstance inst{};
  inst.type = type;
  inst.remaining_sec = duration_sec;
  inst.stacks = add_stacks;
  inst.magnitude = magnitude;

  const StatusSpec spec = default_spec(type);
  inst.tick_accumulator_sec = (spec.tick_interval_sec > 0.0f) ? spec.tick_interval_sec : 0.0f;

  statuses.push_back(inst);
}

struct StatusTickOutput {
  DamagePacket dot_damage{};
  StatusAggregate aggregate{};
};

inline StatusTickOutput tick_statuses(std::vector<StatusInstance>& statuses, float dt_sec) {
  StatusTickOutput out{};
  dt_sec = std::max(0.0f, dt_sec);

  // Update & apply DOT
  for (auto& st : statuses) {
    st.remaining_sec -= dt_sec;
    const StatusSpec spec = default_spec(st.type);

    // Aggregate influence
    out.aggregate.accuracy_mult *= spec.accuracy_mult;
    out.aggregate.move_mult *= spec.move_mult;
    out.aggregate.blocks_attacks = out.aggregate.blocks_attacks || spec.blocks_attacks;

    if (spec.tick_interval_sec <= 0.0f || spec.dot_per_tick.is_zero()) continue;

    st.tick_accumulator_sec -= dt_sec;

    // Apply multiple ticks if dt is large. Cap to avoid pathological loops.
    int tick_guard = 0;
    while (st.tick_accumulator_sec <= 0.0f && tick_guard++ < 16) {
      // DOT stacks scale linearly; magnitude can scale certain effects.
      for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
        out.dot_damage.amount[i] += spec.dot_per_tick.amount[i] *
                                    static_cast<float>(st.stacks) * st.magnitude;
      }
      st.tick_accumulator_sec += spec.tick_interval_sec;
    }
    if (tick_guard >= 16) {
      // Reset rather than potentially spiral.
      st.tick_accumulator_sec = spec.tick_interval_sec;
    }
  }

  // Remove expired
  statuses.erase(
      std::remove_if(statuses.begin(), statuses.end(),
                     [](const StatusInstance& s) { return s.remaining_sec <= 0.0f; }),
      statuses.end());

  // Clamp aggregate multipliers to sane ranges
  out.aggregate.accuracy_mult = std::clamp(out.aggregate.accuracy_mult, 0.0f, 2.0f);
  out.aggregate.move_mult = std::clamp(out.aggregate.move_mult, 0.0f, 2.0f);

  return out;
}

} // namespace colony::combat
