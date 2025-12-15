#pragma once

#include <cstdint>
#include <limits>

namespace colony::combat {

// PCG32: small, fast, deterministic RNG (good for gameplay randomness).
// Reference algorithm: PCG family (O'Neill). This is a minimal implementation.
class Rng final {
public:
  constexpr Rng() = default;

  explicit constexpr Rng(std::uint64_t seed, std::uint64_t sequence = 0xDA3E39CB94B95BDBULL) noexcept {
    seed_rng(seed, sequence);
  }

  constexpr void seed_rng(std::uint64_t seed, std::uint64_t sequence = 0xDA3E39CB94B95BDBULL) noexcept {
    state_ = 0U;
    inc_ = (sequence << 1U) | 1U;
    (void)next_u32();
    state_ += seed;
    (void)next_u32();
  }

  [[nodiscard]] constexpr std::uint32_t next_u32() noexcept {
    const std::uint64_t oldstate = state_;
    state_ = oldstate * 6364136223846793005ULL + inc_;
    const std::uint32_t xorshifted =
        static_cast<std::uint32_t>(((oldstate >> 18U) ^ oldstate) >> 27U);
    const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((0U - rot) & 31U));
  }

  // Uniform in [0, bound) without modulo bias.
  [[nodiscard]] constexpr std::uint32_t uniform_u32(std::uint32_t bound) noexcept {
    if (bound == 0U) return 0U;

    const std::uint32_t threshold = static_cast<std::uint32_t>(0U - bound) % bound;
    for (;;) {
      const std::uint32_t r = next_u32();
      if (r >= threshold) return r % bound;
    }
  }

  // Float in [0, 1). Uses 24 high bits for stable precision.
  [[nodiscard]] constexpr float next_float01() noexcept {
    constexpr float inv = 1.0f / 16777216.0f; // 2^24
    return static_cast<float>(next_u32() >> 8) * inv;
  }

private:
  std::uint64_t state_{0x853C49E6748FEA9BULL};
  std::uint64_t inc_{0xDA3E39CB94B95BDBULL};
};

} // namespace colony::combat
