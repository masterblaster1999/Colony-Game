#pragma once

#include "CGCombatTypes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace colony::combat {

struct DamagePacket {
  std::array<float, kDamageTypeCount> amount{};

  [[nodiscard]] static constexpr DamagePacket none() noexcept { return {}; }

  [[nodiscard]] static constexpr DamagePacket single(DamageType type, float value) noexcept {
    DamagePacket p{};
    p.amount[static_cast<std::size_t>(type)] = value;
    return p;
  }

  [[nodiscard]] constexpr bool is_zero(float eps = 0.0001f) const noexcept {
    for (float v : amount) {
      if (v > eps) return false;
    }
    return true;
  }
};

struct Resistances {
  // Percentage reduction per type in [0,1]. Example: 0.25 => 25% less damage.
  std::array<float, kDamageTypeCount> pct{};

  // Flat reduction applied before percentage (per type).
  std::array<float, kDamageTypeCount> flat{};

  [[nodiscard]] static constexpr Resistances none() noexcept { return {}; }
};

struct DamageResult {
  std::array<float, kDamageTypeCount> applied{};
  float total{0.0f};
};

[[nodiscard]] inline DamageResult apply_damage(
    const DamagePacket& incoming,
    const Resistances& res,
    float general_flat_armor) noexcept
{
  DamageResult out{};

  for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
    const float in = incoming.amount[i];
    if (in <= 0.0f) continue;

    // TrueDamage bypasses armor/resistance by convention.
    const bool is_true = (i == static_cast<std::size_t>(DamageType::TrueDamage));
    float x = in;

    if (!is_true) {
      x = std::max(0.0f, x - general_flat_armor);
      x = std::max(0.0f, x - res.flat[i]);

      const float pct = std::clamp(res.pct[i], 0.0f, 0.95f); // clamp to avoid invulnerability
      x *= (1.0f - pct);
    }

    out.applied[i] = x;
    out.total += x;
  }

  return out;
}

[[nodiscard]] inline DamagePacket operator+(const DamagePacket& a, const DamagePacket& b) noexcept {
  DamagePacket out{};
  for (std::size_t i = 0; i < kDamageTypeCount; ++i) out.amount[i] = a.amount[i] + b.amount[i];
  return out;
}

[[nodiscard]] inline DamagePacket& operator+=(DamagePacket& a, const DamagePacket& b) noexcept {
  for (std::size_t i = 0; i < kDamageTypeCount; ++i) a.amount[i] += b.amount[i];
  return a;
}

} // namespace colony::combat
