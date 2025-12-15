#include "CGCombatWorld.hpp"

#include <algorithm>

namespace colony::combat {

CombatWorld::CombatWorld() = default;

void CombatWorld::reserve(std::size_t combatant_count) {
  combatants_.reserve(combatant_count);
  index_by_id_.reserve(combatant_count);
  attack_queue_.reserve(std::max<std::size_t>(32, combatant_count / 4));
  events_.reserve(std::max<std::size_t>(64, combatant_count / 2));
}

bool CombatWorld::contains(EntityId id) const {
  return index_by_id_.find(id) != index_by_id_.end();
}

Combatant& CombatWorld::create_or_get(EntityId id) {
  const auto it = index_by_id_.find(id);
  if (it != index_by_id_.end()) {
    return combatants_[it->second];
  }

  const std::size_t idx = combatants_.size();
  combatants_.push_back(Combatant{});
  Combatant& c = combatants_.back();
  c.id = id;

  init_weapon_state(c.weapon_state, c.weapon);

  index_by_id_.emplace(id, idx);
  return c;
}

Combatant* CombatWorld::try_get(EntityId id) {
  const auto it = index_by_id_.find(id);
  if (it == index_by_id_.end()) return nullptr;
  return &combatants_[it->second];
}

const Combatant* CombatWorld::try_get(EntityId id) const {
  const auto it = index_by_id_.find(id);
  if (it == index_by_id_.end()) return nullptr;
  return &combatants_[it->second];
}

bool CombatWorld::destroy(EntityId id) {
  const auto it = index_by_id_.find(id);
  if (it == index_by_id_.end()) return false;
  remove_index_at(it->second);
  return true;
}

void CombatWorld::remove_index_at(std::size_t idx) {
  const std::size_t last = combatants_.empty() ? 0U : (combatants_.size() - 1U);
  if (idx >= combatants_.size()) return;

  const EntityId id_to_remove = combatants_[idx].id;

  if (idx != last) {
    std::swap(combatants_[idx], combatants_[last]);
    // Update swapped element index
    index_by_id_[combatants_[idx].id] = idx;
  }

  combatants_.pop_back();
  index_by_id_.erase(id_to_remove);
}

void CombatWorld::enqueue_attack(const AttackRequest& req) {
  if (req.attacker == kInvalidEntity) return;
  attack_queue_.push_back(req);

  CombatEvent e{};
  e.type = CombatEventType::AttackRequested;
  e.source = req.attacker;
  e.target = req.target;
  push_event(e);
}

void CombatWorld::clear_attack_queue() {
  attack_queue_.clear();
}

void CombatWorld::rebuild_spatial_index(float cell_size) {
  spatial_.set_cell_size(cell_size);
  spatial_.clear();
  spatial_.reserve(combatants_.size());

  for (const Combatant& c : combatants_) {
    if (!c.alive) continue;
    spatial_.insert(c.id, c.position);
  }

  spatial_ready_ = true;
}

} // namespace colony::combat
