#pragma once

#include "CGCombatWorld.hpp"
#include "CGCombatRng.hpp"

#include <cstdint>

namespace colony::combat {

struct CombatEnvironment {
  void* user{nullptr};

  // Return true if attacker has line of sight to target. If null, assumed true.
  bool (*has_line_of_sight)(void* user, EntityId attacker, EntityId target,
                            Vec2 attacker_pos, Vec2 target_pos){nullptr};

  // Return [0,1] where 0 = no cover, 1 = full cover. If null, assumed 0.
  float (*cover_amount)(void* user, EntityId attacker, EntityId target,
                        Vec2 attacker_pos, Vec2 target_pos){nullptr};

  // Optional notification when an entity dies.
  void (*on_unit_died)(void* user, EntityId dead, EntityId killer){nullptr};
};

struct CombatSimConfig {
  float fixed_dt_sec{1.0f / 30.0f};
  std::uint32_t max_substeps{8};

  bool allow_friendly_fire{false};
  float spatial_cell_size{2.0f};

  // Simple tuning knobs for hit chance falloff.
  float range_falloff_strength{0.5f}; // 0 => none, 1 => strong
};

class CombatSimulation final {
public:
  CombatSimulation();

  [[nodiscard]] CombatWorld& world() noexcept { return world_; }
  [[nodiscard]] const CombatWorld& world() const noexcept { return world_; }

  [[nodiscard]] CombatSimConfig& config() noexcept { return config_; }
  [[nodiscard]] const CombatSimConfig& config() const noexcept { return config_; }

  void seed(std::uint64_t seed, std::uint64_t sequence = 0xDA3E39CB94B95BDBULL) noexcept {
    rng_.seed_rng(seed, sequence);
  }

  // Advance the simulation by dt (seconds). Uses fixed time steps for determinism.
  void update(float dt_sec, const CombatEnvironment& env = {});

  // Convenience: queue an attack on a target entity.
  void request_attack(EntityId attacker, EntityId target);

  // Convenience: queue a point/ground attack (useful for AoE weapons).
  void request_attack_point(EntityId attacker, Vec2 point);

private:
  CombatWorld world_{};
  CombatSimConfig config_{};
  Rng rng_{0xC0FFEEULL, 0xBADC0DEULL};

  float accumulator_sec_{0.0f};
  float sim_time_sec_{0.0f};

  void step_fixed(float dt_sec, const CombatEnvironment& env);

  void process_regen_and_statuses(float dt_sec);
  void process_weapons(float dt_sec);

  void resolve_attacks(const CombatEnvironment& env);
  void resolve_single_attack(const AttackRequest& req, const CombatEnvironment& env);

  [[nodiscard]] bool is_valid_attack_pair(const Combatant& attacker, const Combatant& target) const noexcept;
  [[nodiscard]] float compute_hit_chance(const Combatant& attacker, const Combatant* target,
                                        float distance_to_target, float cover) const noexcept;

  void apply_damage_and_maybe_kill(EntityId attacker_id, Combatant& victim,
                                  const DamagePacket& raw_damage, bool critical,
                                  const CombatEnvironment& env);
};

} // namespace colony::combat
