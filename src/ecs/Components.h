// src/ecs/Components.h
#pragma once
#include <functional>
#include <string>

#include "core/Profile.h"

#include <entt/entt.hpp> // required

namespace colony::ecs {

struct Name { std::string value; };

// Simple 2D transform; expand as needed.
struct Transform {
  float x = 0.f, y = 0.f;
  float rot = 0.f;
  float sx = 1.f, sy = 1.f;
};

// Per-entity tick callback (gameplay logic, AI, etc.)
struct Tickable {
  // signature: void(entt::registry&, entt::entity, double dt_seconds)
  std::function<void(entt::registry&, entt::entity, double)> tick;
  bool active = true;
};

// Per-entity render callback; uses interpolation alpha.
struct Renderable {
  // signature: void(entt::registry&, entt::entity, float alpha)
  std::function<void(entt::registry&, entt::entity, float)> draw;
  bool visible = true;
};

// Example "heavy" component to show parallel updates.
struct Growth {
  float rate  = 0.0f;  // units per second
  float value = 0.0f;  // accumulated
};

} // namespace colony::ecs
