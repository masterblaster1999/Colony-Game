#include "CGCombatSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace colony::combat {

namespace {
[[nodiscard]] float clamp01(float v) noexcept {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

[[nodiscard]] Vec2 random_in_unit_circle(Rng& rng) {
  // Simple rejection sampling.
  for (int guard = 0; guard < 16; ++guard) {
    const float x = rng.next_float01() * 2.0f - 1.0f;
    const float y = rng.next_float01() * 2.0f - 1.0f;
    const Vec2 v{x, y};
    if (length_sq(v) <= 1.0f) return v;
  }
  // Fallback: deterministic axis.
  return {1.0f, 0.0f};
}

[[nodiscard]] DamagePacket scale_damage(const DamagePacket& in, float s) {
  DamagePacket out{};
  for (std::size_t i = 0; i < kDamageTypeCount; ++i) out.amount[i] = in.amount[i] * s;
  return out;
}
} // namespace

CombatSimulation::CombatSimulation() = default;

void CombatSimulation::request_attack(EntityId attacker, EntityId target) {
  AttackRequest r{};
  r.attacker = attacker;
  r.target = target;
  r.aim_at_point = false;
  world_.enqueue_attack(r);
}

void CombatSimulation::request_attack_point(EntityId attacker, Vec2 point) {
  AttackRequest r{};
  r.attacker = attacker;
  r.target = kInvalidEntity;
  r.aim_at_point = true;
  r.aim_point = point;
  world_.enqueue_attack(r);
}

void CombatSimulation::update(float dt_sec, const CombatEnvironment& env) {
  dt_sec = std::max(0.0f, dt_sec);

  // Rebuild spatial index once per outer update. If your game moves combatants
  // multiple times per frame, you can also call rebuild_spatial_index manually.
  world_.rebuild_spatial_index(config_.spatial_cell_size);

  accumulator_sec_ += dt_sec;

  const float fixed = std::max(1.0f / 240.0f, config_.fixed_dt_sec);
  std::uint32_t steps = 0;

  // Keep events for this external update() call.
  world_.clear_events();

  while (accumulator_sec_ >= fixed && steps < config_.max_substeps) {
    step_fixed(fixed, env);
    accumulator_sec_ -= fixed;
    sim_time_sec_ += fixed;
    ++steps;
  }

  if (steps >= config_.max_substeps) {
    // Avoid spiral-of-death. Drop leftover time.
    accumulator_sec_ = 0.0f;
  }
}

void CombatSimulation::step_fixed(float dt_sec, const CombatEnvironment& env) {
  (void)env;
  process_regen_and_statuses(dt_sec);
  process_weapons(dt_sec);
  resolve_attacks(env);
}

void CombatSimulation::process_regen_and_statuses(float dt_sec) {
  for (Combatant& c : world_.combatants()) {
    if (!c.alive) continue;

    if (c.health_regen_per_sec != 0.0f) {
      c.health = std::clamp(c.health + c.health_regen_per_sec * dt_sec, 0.0f, c.max_health);
    }

    StatusTickOutput st = tick_statuses(c.statuses, dt_sec);
    c.status_mods = st.aggregate;

    if (!st.dot_damage.is_zero()) {
      apply_damage_and_maybe_kill(kInvalidEntity, c, st.dot_damage, false, {});
    }
  }
}

void CombatSimulation::process_weapons(float dt_sec) {
  for (Combatant& c : world_.combatants()) {
    if (!c.alive) continue;

    // Auto-reload policy: if empty and has reserves, start reload.
    if (c.weapon.uses_ammo && !c.weapon_state.reloading) {
      if (c.weapon_state.ammo_in_mag <= 0 && c.weapon_state.reserve_ammo > 0) {
        start_reload(c.weapon_state, c.weapon);
        if (c.weapon_state.reloading) {
          CombatEvent e{};
          e.type = CombatEventType::ReloadStarted;
          e.source = c.id;
          world_.push_event(e);
        }
      }
    }

    const bool was_reloading = c.weapon_state.reloading;
    tick_weapon(c.weapon_state, c.weapon, dt_sec);

    if (was_reloading && !c.weapon_state.reloading) {
      CombatEvent e{};
      e.type = CombatEventType::ReloadFinished;
      e.source = c.id;
      world_.push_event(e);
    }
  }
}

void CombatSimulation::resolve_attacks(const CombatEnvironment& env) {
  // Copy out requests to avoid re-entrancy issues.
  const std::span<const AttackRequest> q = world_.attack_queue();
  std::vector<AttackRequest> requests;
  requests.assign(q.begin(), q.end());
  world_.clear_attack_queue();

  for (const AttackRequest& req : requests) {
    resolve_single_attack(req, env);
  }
}

bool CombatSimulation::is_valid_attack_pair(const Combatant& attacker, const Combatant& target) const noexcept {
  if (!attacker.alive || !target.alive) return false;
  if (attacker.id == target.id) return false;

  if (!config_.allow_friendly_fire) {
    if (attacker.faction != kNeutralFaction && attacker.faction == target.faction) return false;
  }

  return true;
}

float CombatSimulation::compute_hit_chance(
    const Combatant& attacker,
    const Combatant* target,
    float distance_to_target,
    float cover) const noexcept
{
  float chance = clamp01(attacker.weapon.accuracy);
  chance *= std::max(0.0f, attacker.accuracy_skill);
  chance *= attacker.status_mods.accuracy_mult;

  // Range falloff (simple linear).
  if (attacker.weapon.range > 0.0f) {
    const float dn = std::clamp(distance_to_target / attacker.weapon.range, 0.0f, 1.25f);
    const float falloff = std::clamp(1.0f - config_.range_falloff_strength * dn, 0.2f, 1.0f);
    chance *= falloff;
  }

  // Target dodge.
  if (target) {
    const float dodge = std::clamp(target->dodge, 0.0f, 0.9f);
    chance *= (1.0f - dodge);
  }

  // Cover.
  cover = std::clamp(cover, 0.0f, 0.95f);
  chance *= (1.0f - cover);

  // Clamp to [min, max].
  chance = std::clamp(chance, attacker.weapon.min_hit_chance, 0.95f);
  return chance;
}

void CombatSimulation::resolve_single_attack(const AttackRequest& req, const CombatEnvironment& env) {
  Combatant* attacker = world_.try_get(req.attacker);
  if (!attacker || !attacker->alive) return;

  Combatant* target = nullptr;
  if (!req.aim_at_point && req.target != kInvalidEntity) {
    target = world_.try_get(req.target);
    if (!target) return;
    if (!is_valid_attack_pair(*attacker, *target)) return;
  }

  if (attacker->status_mods.blocks_attacks) return;

  // Ensure weapon is ready.
  if (!can_fire(attacker->weapon_state, attacker->weapon)) {
    return;
  }

  // Aim point
  Vec2 aim = {};
  if (req.aim_at_point) {
    aim = req.aim_point;
  } else if (target) {
    aim = target->position;
  } else {
    return;
  }

  const float dist = distance(attacker->position, aim);
  if (attacker->weapon.range > 0.0f && dist > attacker->weapon.range) {
    return;
  }

  // LoS / cover
  float cover = 0.0f;
  if (target) {
    if (env.has_line_of_sight) {
      if (!env.has_line_of_sight(env.user, attacker->id, target->id, attacker->position, target->position)) {
        return;
      }
    }
    if (env.cover_amount) {
      cover = env.cover_amount(env.user, attacker->id, target->id, attacker->position, target->position);
    }
  }

  // Point-attacks: convert accuracy into scatter rather than hit/miss.
  bool hit = true;
  float hit_chance = 1.0f;
  if (!req.aim_at_point && target) {
    hit_chance = compute_hit_chance(*attacker, target, dist, cover);
    hit = (rng_.next_float01() < hit_chance);
  } else {
    // Scatter radius grows as accuracy drops.
    const float acc = compute_hit_chance(*attacker, nullptr, dist, 0.0f);
    const float scatter = (1.0f - acc) * std::max(0.5f, attacker->weapon.aoe_radius);
    if (scatter > 0.0f) {
      const Vec2 off = random_in_unit_circle(rng_) * scatter;
      aim += off;
    }
  }

  // Consume ammo and set cooldown regardless of hit.
  consume_ammo_and_trigger_cooldown(attacker->weapon_state, attacker->weapon);

  if (!hit) {
    CombatEvent miss{};
    miss.type = CombatEventType::AttackMissed;
    miss.source = attacker->id;
    miss.target = target ? target->id : kInvalidEntity;
    world_.push_event(miss);

    // For AoE weapons: a miss can still land nearby (scatter).
    if (attacker->weapon.aoe_radius <= 0.0f) {
      return;
    }

    const float scatter = (1.0f - hit_chance) * attacker->weapon.aoe_radius;
    if (scatter > 0.0f && target) {
      const Vec2 off = random_in_unit_circle(rng_) * scatter;
      aim = target->position + off;
    }
  } else {
    CombatEvent ev{};
    ev.type = CombatEventType::AttackHit;
    ev.source = attacker->id;
    ev.target = target ? target->id : kInvalidEntity;
    world_.push_event(ev);
  }

  // Crit roll (only matters if we actually apply damage to someone)
  const bool is_crit = (rng_.next_float01() < clamp01(attacker->weapon.crit_chance));
  const float crit_mult = is_crit ? std::max(1.0f, attacker->weapon.crit_multiplier) : 1.0f;

  DamagePacket shot_damage = scale_damage(attacker->weapon.damage, crit_mult);

  // Apply either single-target or AoE damage.
  if (attacker->weapon.aoe_radius > 0.0f) {
    std::vector<EntityId> victims;
    victims.reserve(16);

    world_.query_circle(aim, attacker->weapon.aoe_radius, [&](const Combatant& c) {
      victims.push_back(c.id);
    });

    for (EntityId vid : victims) {
      if (vid == attacker->id) continue; // no self-damage by default
      Combatant* victim = world_.try_get(vid);
      if (!victim || !victim->alive) continue;

      if (!config_.allow_friendly_fire) {
        if (attacker->faction != kNeutralFaction && attacker->faction == victim->faction) continue;
      }

      const float d = distance(victim->position, aim);
      const float falloff = std::clamp(1.0f - (d / attacker->weapon.aoe_radius), 0.1f, 1.0f);
      const DamagePacket dmg = scale_damage(shot_damage, falloff);

      apply_damage_and_maybe_kill(attacker->id, *victim, dmg, is_crit, env);

      // Status proc on hit (if still alive).
      if (victim->alive && attacker->weapon.on_hit.chance > 0.0f) {
        if (rng_.next_float01() < clamp01(attacker->weapon.on_hit.chance)) {
          add_or_refresh(victim->statuses,
                         attacker->weapon.on_hit.effect,
                         attacker->weapon.on_hit.duration_sec,
                         attacker->weapon.on_hit.stacks,
                         attacker->weapon.on_hit.stack_cap,
                         attacker->weapon.on_hit.magnitude);

          CombatEvent se{};
          se.type = CombatEventType::StatusApplied;
          se.source = attacker->id;
          se.target = victim->id;
          se.status = attacker->weapon.on_hit.effect;
          se.stacks = attacker->weapon.on_hit.stacks;
          world_.push_event(se);
        }
      }
    }
  } else if (target && hit) {
    apply_damage_and_maybe_kill(attacker->id, *target, shot_damage, is_crit, env);

    if (target->alive && attacker->weapon.on_hit.chance > 0.0f) {
      if (rng_.next_float01() < clamp01(attacker->weapon.on_hit.chance)) {
        add_or_refresh(target->statuses,
                       attacker->weapon.on_hit.effect,
                       attacker->weapon.on_hit.duration_sec,
                       attacker->weapon.on_hit.stacks,
                       attacker->weapon.on_hit.stack_cap,
                       attacker->weapon.on_hit.magnitude);

        CombatEvent se{};
        se.type = CombatEventType::StatusApplied;
        se.source = attacker->id;
        se.target = target->id;
        se.status = attacker->weapon.on_hit.effect;
        se.stacks = attacker->weapon.on_hit.stacks;
        world_.push_event(se);
      }
    }
  }
}

void CombatSimulation::apply_damage_and_maybe_kill(
    EntityId attacker_id,
    Combatant& victim,
    const DamagePacket& raw_damage,
    bool critical,
    const CombatEnvironment& env)
{
  if (!victim.alive) return;

  const DamageResult dr = apply_damage(raw_damage, victim.resist, victim.general_flat_armor);
  if (dr.total <= 0.0f) return;

  victim.health = std::max(0.0f, victim.health - dr.total);

  CombatEvent dmg{};
  dmg.type = CombatEventType::DamageApplied;
  dmg.source = attacker_id;
  dmg.target = victim.id;
  dmg.total_damage = dr.total;
  dmg.critical = critical;

  // Store applied damage breakdown
  for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
    dmg.damage.amount[i] = dr.applied[i];
  }
  world_.push_event(dmg);

  if (victim.health <= 0.0f && victim.alive) {
    victim.alive = false;

    CombatEvent died{};
    died.type = CombatEventType::UnitDied;
    died.source = attacker_id;
    died.target = victim.id;
    world_.push_event(died);

    if (env.on_unit_died) {
      env.on_unit_died(env.user, victim.id, attacker_id);
    }
  }
}

} // namespace colony::combat
