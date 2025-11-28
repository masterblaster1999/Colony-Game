// include/procgen/Biome.h
#pragma once
// Single authoritative definition of procgen::Biome.
// NOTE: Remove any duplicate Biome definitions elsewhere (e.g., in Types.h).

#include <cstdint>
#include <array>
#include <string_view>
#include <optional>
#include <type_traits>
#include <algorithm>
#include <ostream>
#include <cctype>
#include <functional>

namespace procgen {

//------------------------------------------------------------------------------
// Core type
//------------------------------------------------------------------------------
enum class Biome : std::uint8_t {
    // Order chosen to preserve legacy expectations seen in older Types.h
    Ocean = 0,
    Beach,
    Desert,
    Grassland,
    Forest,
    Rainforest, // <--- restored
    Savanna,
    Taiga,
    Tundra,
    Snow,
    Mountain
};

static_assert(std::is_enum_v<Biome>, "Biome must be an enum type");
static_assert(sizeof(Biome) == 1,     "Biome must remain 1 byte");

// Constant-time conversion to the underlying representation (C++23 has std::to_underlying)
template <class E>
[[nodiscard]] constexpr std::underlying_type_t<E> to_underlying(E e) noexcept {
    static_assert(std::is_enum_v<E>, "to_underlying requires an enum");
    return static_cast<std::underlying_type_t<E>>(e);
}

//------------------------------------------------------------------------------
// Enumerant inventory / iteration helpers
//------------------------------------------------------------------------------
inline constexpr std::array<Biome, 11> kAllBiomes{
    Biome::Ocean, Biome::Beach, Biome::Desert, Biome::Grassland, Biome::Forest,
    Biome::Rainforest, Biome::Savanna, Biome::Taiga, Biome::Tundra, Biome::Snow, Biome::Mountain
};

[[nodiscard]] constexpr std::size_t biome_count() noexcept { return kAllBiomes.size(); }

[[nodiscard]] constexpr const Biome* begin_biomes() noexcept { return kAllBiomes.data(); }
[[nodiscard]] constexpr const Biome* end_biomes()   noexcept { return kAllBiomes.data() + kAllBiomes.size(); }

//------------------------------------------------------------------------------
// String conversions
//------------------------------------------------------------------------------
using namespace std::literals;

[[nodiscard]] constexpr std::string_view to_string(Biome b) noexcept {
    switch (b) {
        case Biome::Ocean:      return "Ocean"sv;
        case Biome::Beach:      return "Beach"sv;
        case Biome::Desert:     return "Desert"sv;
        case Biome::Grassland:  return "Grassland"sv;
        case Biome::Forest:     return "Forest"sv;
        case Biome::Rainforest: return "Rainforest"sv;
        case Biome::Savanna:    return "Savanna"sv;
        case Biome::Taiga:      return "Taiga"sv;
        case Biome::Tundra:     return "Tundra"sv;
        case Biome::Snow:       return "Snow"sv;
        case Biome::Mountain:   return "Mountain"sv;
        default:                return "Unknown"sv;
    }
}

[[nodiscard]] inline bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ac = static_cast<unsigned char>(a[i]);
        unsigned char bc = static_cast<unsigned char>(b[i]);
        if (std::tolower(ac) != std::tolower(bc)) return false;
    }
    return true;
}

[[nodiscard]] inline std::optional<Biome> parse_biome(std::string_view name, bool case_insensitive = true) noexcept {
    for (Biome b : kAllBiomes) {
        const auto sv = to_string(b);
        if ((case_insensitive && iequals(sv, name)) || (!case_insensitive && sv == name)) {
            return b;
        }
    }
    return std::nullopt;
}

// ostream helper (useful for logging)
inline std::ostream& operator<<(std::ostream& os, Biome b) {
    return os << to_string(b);
}

//------------------------------------------------------------------------------
// Packing & validation
//------------------------------------------------------------------------------
[[nodiscard]] constexpr std::uint8_t pack(Biome b) noexcept { return static_cast<std::uint8_t>(b); }

[[nodiscard]] inline std::optional<Biome> unpack_checked(std::uint8_t v) noexcept {
    return (v < biome_count()) ? std::optional<Biome>(static_cast<Biome>(v)) : std::nullopt;
}

//------------------------------------------------------------------------------
// Debug visualization: ARGB color (0xAARRGGBB) per biome
//------------------------------------------------------------------------------
[[nodiscard]] constexpr std::uint32_t biome_color_argb(Biome b) noexcept {
    switch (b) {
        case Biome::Ocean:      return 0xFF1F4E79u; // deep blue
        case Biome::Beach:      return 0xFFF7E9A8u; // sand
        case Biome::Desert:     return 0xFFCCB36Cu; // tan
        case Biome::Grassland:  return 0xFF7FBF7Fu; // green
        case Biome::Forest:     return 0xFF2F6B2Fu; // dark green
        case Biome::Rainforest: return 0xFF0F5F2Fu; // lush deep green
        case Biome::Savanna:    return 0xFFD7C67Fu; // yellow-green
        case Biome::Taiga:      return 0xFF2C5F5Fu; // teal
        case Biome::Tundra:     return 0xFF9FB4C8u; // pale blue-gray
        case Biome::Snow:       return 0xFFFFFFFFu; // white
        case Biome::Mountain:   return 0xFF7A7A7Au; // rock
        default:                return 0xFFFF00FFu; // magenta for unknown
    }
}

//------------------------------------------------------------------------------
// Tunable classification with explicit thresholds
//------------------------------------------------------------------------------
struct BiomeThresholds {
    // Elevation (0..1)
    float ocean_elev_max    = 0.02f;
    float beach_elev_max    = 0.06f;
    float mountain_elev_min = 0.75f;

    // Temperature (Celsius)
    float snow_tempC        = -5.0f;  // above mountains or very cold regions
    float cold_tempC        = 5.0f;   // boundary between Tundra/Taiga decisions
    float temperate_tempC   = 18.0f;  // boundary between temperate vs warm

    // Moisture (0..1)
    float desert_moisture   = 0.25f;
    float grass_moisture    = 0.55f;
    float savanna_moisture  = 0.60f;
    float taiga_moisture    = 0.50f;
    float rainforest_moist  = 0.75f;  // NEW: very wet → Rainforest when warm
};

// Clamp helpers (kept inline for zero overhead)
[[nodiscard]] constexpr float clamp01(float v) noexcept {
    return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

[[nodiscard]] inline Biome classify_biome(
    float elev, float moisture, float tempC,
    const BiomeThresholds& t = {}
) noexcept
{
    elev     = clamp01(elev);
    moisture = clamp01(moisture);

    if (elev < t.ocean_elev_max)  return Biome::Ocean;
    if (elev < t.beach_elev_max)  return Biome::Beach;

    if (elev > t.mountain_elev_min)
        return (tempC < t.snow_tempC) ? Biome::Snow : Biome::Mountain;

    if (tempC < t.snow_tempC)     return Biome::Tundra;
    if (tempC < t.cold_tempC)     return (moisture > t.taiga_moisture) ? Biome::Taiga : Biome::Tundra;

    if (tempC < t.temperate_tempC) {
        if (moisture < t.desert_moisture) return Biome::Desert;
        if (moisture < t.grass_moisture)  return Biome::Grassland;
        return Biome::Forest;
    }

    // Warm
    if (moisture < t.desert_moisture)  return Biome::Desert;
    if (moisture < t.savanna_moisture) return Biome::Savanna;
    if (moisture >= t.rainforest_moist) return Biome::Rainforest;
    return Biome::Forest;
}

} // namespace procgen

//------------------------------------------------------------------------------
// Hash support (useful for unordered_map/set keyed by Biome)
//------------------------------------------------------------------------------
namespace std {
template <>
struct hash<procgen::Biome> {
    size_t operator()(procgen::Biome b) const noexcept {
        return static_cast<size_t>(procgen::to_underlying(b));
    }
};
} // namespace std
