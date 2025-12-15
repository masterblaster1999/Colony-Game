#pragma once

#include "CGCombatTypes.hpp"
#include "CGDamageModel.hpp"
#include "CGStatusEffects.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace colony::combat {

struct StatusProc {
  StatusEffect effect{StatusEffect::Bleeding};
  float chance{0.0f};         // [0,1]
  float duration_sec{0.0f};   // how long the effect lasts
  std::uint16_t stacks{1};
  std::uint16_t stack_cap{5};
  float magnitude{1.0f};
};

struct WeaponDef {
  std::string name{"Weapon"};

  // Firing behavior
  float range{8.0f};                // world units
  float cooldown_sec{0.8f};         // time between shots
  float accuracy{0.75f};            // base hit chance before mods
  float min_hit_chance{0.05f};      // floor hit chance
  float crit_chance{0.05f};         // [0,1]
  float crit_multiplier{1.75f};     // multiplier on damage on crit

  // Damage payload
  DamagePacket damage{DamagePacket::single(DamageType::Kinetic, 5.0f)};

  // AoE: if > 0, apply damage in radius around target point (including target).
  float aoe_radius{0.0f};

  // Ammo (optional)
  bool uses_ammo{false};
  std::int32_t magazine_size{0};
  std::int32_t max_reserve_ammo{0};
  float reload_time_sec{0.0f};

  // On-hit status effect proc (optional)
  StatusProc on_hit{};
};

struct WeaponState {
  float cooldown_remaining_sec{0.0f};

  // Ammo state
  std::int32_t ammo_in_mag{0};
  std::int32_t reserve_ammo{0};
  float reload_remaining_sec{0.0f};
  bool reloading{false};
};

inline void init_weapon_state(WeaponState& st, const WeaponDef& def) {
  st.cooldown_remaining_sec = 0.0f;
  st.reloading = false;
  st.reload_remaining_sec = 0.0f;

  if (def.uses_ammo) {
    st.ammo_in_mag = std::max<std::int32_t>(0, def.magazine_size);
    st.reserve_ammo = std::max<std::int32_t>(0, def.max_reserve_ammo);
  } else {
    st.ammo_in_mag = 0;
    st.reserve_ammo = 0;
  }
}

[[nodiscard]] inline bool can_fire(const WeaponState& st, const WeaponDef& def) {
  if (st.reloading) return false;
  if (st.cooldown_remaining_sec > 0.0f) return false;
  if (def.uses_ammo && st.ammo_in_mag <= 0) return false;
  return true;
}

inline void start_reload(WeaponState& st, const WeaponDef& def) {
  if (!def.uses_ammo) return;
  if (st.reloading) return;
  if (def.magazine_size <= 0) return;
  if (st.ammo_in_mag >= def.magazine_size) return;
  if (st.reserve_ammo <= 0) return;

  st.reloading = true;
  st.reload_remaining_sec = std::max(0.0f, def.reload_time_sec);
}

inline void tick_weapon(WeaponState& st, const WeaponDef& def, float dt_sec) {
  dt_sec = std::max(0.0f, dt_sec);

  st.cooldown_remaining_sec = std::max(0.0f, st.cooldown_remaining_sec - dt_sec);

  if (st.reloading) {
    st.reload_remaining_sec = std::max(0.0f, st.reload_remaining_sec - dt_sec);
    if (st.reload_remaining_sec <= 0.0f) {
      // Complete reload
      const std::int32_t need = std::max<std::int32_t>(0, def.magazine_size - st.ammo_in_mag);
      const std::int32_t take = std::min(need, st.reserve_ammo);
      st.ammo_in_mag += take;
      st.reserve_ammo -= take;

      st.reloading = false;
      st.reload_remaining_sec = 0.0f;
    }
  }
}

inline void consume_ammo_and_trigger_cooldown(WeaponState& st, const WeaponDef& def) {
  st.cooldown_remaining_sec = std::max(0.0f, def.cooldown_sec);
  if (def.uses_ammo) {
    st.ammo_in_mag = std::max<std::int32_t>(0, st.ammo_in_mag - 1);
  }
}

} // namespace colony::combat
