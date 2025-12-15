#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string_view>

namespace colony::combat {

// ----------------------------------------------------------------------------
// Basic identifiers
// ----------------------------------------------------------------------------
using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidEntity{0};

using FactionId = std::uint32_t;
inline constexpr FactionId kNeutralFaction{0};

// ----------------------------------------------------------------------------
// Minimal math (kept dependency-free; adapters can be added later)
// ----------------------------------------------------------------------------
struct Vec2 {
  float x{0.0f};
  float y{0.0f};

  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
};

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] constexpr Vec2 operator*(Vec2 v, float s) noexcept { return {v.x * s, v.y * s}; }
[[nodiscard]] constexpr Vec2 operator*(float s, Vec2 v) noexcept { return v * s; }
[[nodiscard]] constexpr Vec2 operator/(Vec2 v, float s) noexcept { return {v.x / s, v.y / s}; }

constexpr Vec2& operator+=(Vec2& a, Vec2 b) noexcept {
  a.x += b.x;
  a.y += b.y;
  return a;
}
constexpr Vec2& operator-=(Vec2& a, Vec2 b) noexcept {
  a.x -= b.x;
  a.y -= b.y;
  return a;
}
constexpr Vec2& operator*=(Vec2& v, float s) noexcept {
  v.x *= s;
  v.y *= s;
  return v;
}
constexpr Vec2& operator/=(Vec2& v, float s) noexcept {
  v.x /= s;
  v.y /= s;
  return v;
}

[[nodiscard]] constexpr float dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
[[nodiscard]] constexpr float length_sq(Vec2 v) noexcept { return dot(v, v); }
[[nodiscard]] inline float length(Vec2 v) noexcept { return std::sqrt(length_sq(v)); }
[[nodiscard]] inline float distance(Vec2 a, Vec2 b) noexcept { return length(a - b); }
[[nodiscard]] constexpr float distance_sq(Vec2 a, Vec2 b) noexcept { return length_sq(a - b); }

[[nodiscard]] inline Vec2 normalize_or_zero(Vec2 v) noexcept {
  const float ls = length_sq(v);
  if (ls <= 0.0f) return {};
  const float inv = 1.0f / std::sqrt(ls);
  return {v.x * inv, v.y * inv};
}

// ----------------------------------------------------------------------------
// Damage model
// ----------------------------------------------------------------------------
enum class DamageType : std::uint8_t {
  Kinetic = 0,   // bullets, impacts
  Pierce  = 1,   // armor piercing
  Fire    = 2,   // burning
  Poison  = 3,   // toxins
  Electric= 4,   // shocks
  Cold    = 5,   // freezing
  TrueDamage = 6, // bypasses armor/resistance
  Count
};

inline constexpr std::size_t kDamageTypeCount =
    static_cast<std::size_t>(DamageType::Count);

[[nodiscard]] constexpr std::string_view to_string(DamageType t) noexcept {
  switch (t) {
    case DamageType::Kinetic:    return "Kinetic";
    case DamageType::Pierce:     return "Pierce";
    case DamageType::Fire:       return "Fire";
    case DamageType::Poison:     return "Poison";
    case DamageType::Electric:   return "Electric";
    case DamageType::Cold:       return "Cold";
    case DamageType::TrueDamage: return "TrueDamage";
    default:                     return "Unknown";
  }
}

} // namespace colony::combat
