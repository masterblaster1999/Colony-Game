#pragma once
#include <cstdint>
#include <array>
#include <string_view>
#include <type_traits>

// ---------- bitmask helpers for enum class ----------
template <typename E>
struct is_bitmask_enum : std::false_type {};
template <typename E>
constexpr bool is_bitmask_enum_v = is_bitmask_enum<E>::value;

template <typename E, std::enable_if_t<is_bitmask_enum_v<E>, int> = 0>
constexpr E operator|(E a, E b) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <typename E, std::enable_if_t<is_bitmask_enum_v<E>, int> = 0>
constexpr E operator&(E a, E b) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <typename E, std::enable_if_t<is_bitmask_enum_v<E>, int> = 0>
constexpr E operator~(E a) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(a));
}
template <typename E, std::enable_if_t<is_bitmask_enum_v<E>, int> = 0>
constexpr bool any(E a) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<U>(a) != 0;
}
template <typename E, std::enable_if_t<is_bitmask_enum_v<E>, int> = 0>
constexpr bool all(E a, E mask) noexcept {
    return (a & mask) == mask;
}

// ---------- capabilities describe what a role may do ----------
enum class Capability : uint32_t {
    None         = 0,
    Hauling      = 1u << 0,
    Building     = 1u << 1,
    Mining       = 1u << 2,
    Farming      = 1u << 3,
    Medical      = 1u << 4,
    Combat       = 1u << 5,
    Research     = 1u << 6,
    Repair       = 1u << 7,
    Firefighting = 1u << 8,
    Logistics    = 1u << 9,  // doors/traffic mgmt, control consoles, etc.
    All          = 0xFFFFFFFFu
};
template <> struct is_bitmask_enum<Capability> : std::true_type {};

// ---------- role ids (extend or reorder freely if you serialize by name) ----------
enum class RoleId : uint8_t {
    Worker = 0,
    Hauler,
    Builder,
    Miner,
    Farmer,
    Medic,
    Guard,
    Researcher,
    Engineer,
    Count
};

// Optional: small pathfinding cost profile per role (weights are 0..255).
// If you don't want path-specific behavior yet, leave defaults at 0.
struct NavCostProfile {
    uint8_t kBase   = 10;  // base tile cost multiplier
    uint8_t kHazard = 0;   // hazard/unsafe tile penalty
    uint8_t kCrowd  = 0;   // congestion penalty
    uint8_t kDoor   = 0;   // door traversal penalty
    uint8_t kNoise  = 0;   // "comfort" penalty near noisy areas
    uint8_t kLight  = 0;   // "comfort" penalty near bright areas (at night)
};

// ---------- role definition ----------
struct RoleDef {
    const char*     name;
    Capability      caps;
    float           moveMult;   // movement speed multiplier (1.0 = baseline)
    float           workMult;   // task speed multiplier
    uint16_t        carryBonus; // additional carry capacity (units)
    NavCostProfile  nav;        // optional: role-aware path costs
};

// ---------- database of roles (tweak to taste) ----------
constexpr std::array<RoleDef, static_cast<size_t>(RoleId::Count)> kRoles = {{
    /* Worker */    {"Worker",    Capability::All,                                      1.00f, 1.00f,  0,  {10,  5,  5,  5,  0,  0}},
    /* Hauler */    {"Hauler",    Capability::Hauling | Capability::Logistics
                                 | Capability::Repair,                                   1.05f, 1.00f, 20,  {10, 10,  0,  0, 10, 10}},
    /* Builder */   {"Builder",   Capability::Building | Capability::Repair,            0.95f, 1.15f,  5,  {10,  5, 10, 10,  0,  0}},
    /* Miner */     {"Miner",     Capability::Mining | Capability::Repair,              0.95f, 1.15f,  5,  {10, 15,  5,  5,  0,  0}},
    /* Farmer */    {"Farmer",    Capability::Farming | Capability::Hauling,            1.00f, 1.10f, 10,  {10,  0,  5,  5,  0,  0}},
    /* Medic */     {"Medic",     Capability::Medical | Capability::Hauling
                                 | Capability::Firefighting,                            1.10f, 1.00f,  5,  {10, 20,  0,  0,  0,  0}}, // avoid hazards
    /* Guard */     {"Guard",     Capability::Combat | Capability::Repair
                                 | Capability::Firefighting,                            1.05f, 1.00f, 10,  {10, 10,  5,  5,  0,  0}},
    /* Researcher */{"Researcher",Capability::Research | Capability::Hauling,           1.00f, 1.10f,  0,  {10,  5,  5, 10,  0,  0}},
    /* Engineer */  {"Engineer",  Capability::Repair | Capability::Building
                                 | Capability::Logistics,                               1.00f, 1.15f,  5,  {10,  5, 10,  5,  0,  0}},
}};

// ---------- tiny utility API ----------
inline constexpr const RoleDef& RoleDefOf(RoleId id) {
    return kRoles[static_cast<size_t>(id)];
}
inline constexpr std::string_view RoleName(RoleId id) {
    return RoleDefOf(id).name;
}
inline constexpr RoleId RoleFromIndex(size_t idx) {
    return static_cast<RoleId>(idx);
}
inline constexpr bool HasAny(Capability have, Capability needAny) {
    return any(have & needAny);
}
inline constexpr bool HasAll(Capability have, Capability needAll) {
    return all(have, needAll);
}

// ---------- drop-in component you can embed in a Pawn ----------
struct RoleComponent {
    RoleId   role     = RoleId::Worker;
    uint16_t level    = 1;         // optional simple progression
    uint32_t xp       = 0;
    static constexpr uint32_t kXpPerLevel = 200;

    // getters
    inline const RoleDef& def()   const { return RoleDefOf(role); }
    inline Capability     caps()  const { return def().caps; }
    inline float          move()  const { return def().moveMult; }
    inline float          work()  const { return def().workMult; }
    inline uint16_t       carry() const { return def().carryBonus; }
    inline NavCostProfile nav()   const { return def().nav; }

    // role changes (notify systems as needed)
    void set(RoleId r) { role = r; /* TODO: fire OnRoleChanged event if you have one */ }

    // dead-simple XP/leveling (optional)
    bool grant_xp(uint32_t add) {
        xp += add;
        bool leveled = false;
        while (xp >= kXpPerLevel) { xp -= kXpPerLevel; level++; leveled = true; }
        return leveled;
    }
};

// ---------- optional helpers for (de)serialization by name ----------
inline constexpr RoleId RoleFromName(std::string_view s) {
    for (size_t i = 0; i < kRoles.size(); ++i)
        if (s == kRoles[i].name) return static_cast<RoleId>(i);
    return RoleId::Worker;
}
