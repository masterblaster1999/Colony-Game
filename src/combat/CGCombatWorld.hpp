#pragma once

#include "CGCombatEvents.hpp"
#include "CGDamageModel.hpp"
#include "CGSpatialHash2D.hpp"
#include "CGStatusEffects.hpp"
#include "CGWeapon.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace colony::combat {

struct Combatant {
  EntityId id{kInvalidEntity};
  FactionId faction{kNeutralFaction};

  Vec2 position{};
  float radius{0.35f}; // used for AoE intersection

  // Core stats
  float max_health{10.0f};
  float health{10.0f};
  float health_regen_per_sec{0.0f};

  // Defense
  float general_flat_armor{0.0f};
  Resistances resist{Resistances::none()};
  float dodge{0.0f}; // [0,0.9] reduces hit chance

  // Offense
  float accuracy_skill{1.0f}; // multiplier on weapon accuracy
  WeaponDef weapon{};
  WeaponState weapon_state{};

  // Effects
  std::vector<StatusInstance> statuses{};

  // Cached, recomputed during simulation tick.
  StatusAggregate status_mods{};

  bool alive{true};

  [[nodiscard]] float health_frac() const noexcept {
    return (max_health > 0.0f) ? (health / max_health) : 0.0f;
  }
};

struct AttackRequest {
  EntityId attacker{kInvalidEntity};
  EntityId target{kInvalidEntity};

  // Optional: ground/point attack (for AoE even without a target)
  bool aim_at_point{false};
  Vec2 aim_point{};
};

class CombatWorld final {
public:
  CombatWorld();

  void reserve(std::size_t combatant_count);

  [[nodiscard]] std::size_t size() const noexcept { return combatants_.size(); }

  [[nodiscard]] bool contains(EntityId id) const;

  Combatant& create_or_get(EntityId id);
  Combatant* try_get(EntityId id);
  const Combatant* try_get(EntityId id) const;

  bool destroy(EntityId id);

  [[nodiscard]] std::span<Combatant> combatants() { return combatants_; }
  [[nodiscard]] std::span<const Combatant> combatants() const { return combatants_; }

  // --------------------------------------------------------------------------
  // Commands & events
  // --------------------------------------------------------------------------
  void enqueue_attack(const AttackRequest& req);
  [[nodiscard]] std::span<const AttackRequest> attack_queue() const { return attack_queue_; }
  void clear_attack_queue();

  void push_event(const CombatEvent& e) { events_.push_back(e); }
  [[nodiscard]] std::span<const CombatEvent> events() const { return events_; }
  void clear_events() { events_.clear(); }

  // --------------------------------------------------------------------------
  // Spatial index (optional but useful)
  // --------------------------------------------------------------------------
  void rebuild_spatial_index(float cell_size = 2.0f);

  template <class Fn>
  void query_circle(Vec2 center, float radius, Fn&& fn) const {
    if (!spatial_ready_) return;

    const float r = std::max(0.0f, radius);
    const float r2 = r * r;

    spatial_.query_circle_candidates(center, r, [&](EntityId id) {
      const Combatant* c = try_get(id);
      if (!c || !c->alive) return;

      const float d2 = distance_sq(c->position, center);
      const float rr = (r + c->radius);
      if (d2 <= rr * rr && d2 <= r2 + 2.0f * r * c->radius + c->radius * c->radius) {
        fn(*c);
      }
    });
  }

private:
  std::vector<Combatant> combatants_{};
  std::unordered_map<EntityId, std::size_t> index_by_id_{};

  std::vector<AttackRequest> attack_queue_{};
  std::vector<CombatEvent> events_{};

  SpatialHash2D spatial_{};
  bool spatial_ready_{false};

  void remove_index_at(std::size_t idx);
};

} // namespace colony::combat
