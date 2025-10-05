#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <functional>

namespace colony
{
    struct IVec2 { int x{0}, y{0}; };

    // Keep the first 6 small and readable for UI/AI rules
    enum class Ethos : uint8_t
    {
        Traders = 0,
        Raiders,
        Settlers,
        Nomads,
        Scholars,
        Cultists
    };

    struct Color { uint8_t r{255}, g{255}, b{255}; };

    struct Faction
    {
        uint32_t    id{0};
        std::string name;
        Ethos       ethos{Ethos::Settlers};
        float       tech{0.5f};         // 0..1
        float       aggression{0.5f};   // 0..1
        float       hospitality{0.5f};  // 0..1
        Color       color{};
        IVec2       base{};             // tile or world coordinate
    };

    struct FactionArchetype
    {
        std::string id;
        Ethos ethos{Ethos::Settlers};
        float weight{1.0f};

        float tech_min{0.2f},         tech_max{0.8f};
        float aggression_min{0.1f},   aggression_max{0.9f};
        float hospitality_min{0.1f},  hospitality_max{0.9f};
    };

    struct FactionGenParams
    {
        uint64_t world_seed{0};
        int map_width{1024};
        int map_height{1024};
        int min_factions{3};
        int max_factions{6};
        int min_base_spacing{128};  // world units / tiles

        // Optional: color palette for factions. If empty, a random HSV tint is generated.
        std::vector<std::array<uint8_t,3>> palette;

        // Archetype bucket to sample from.
        std::vector<FactionArchetype> archetypes;

        // Optional hooks into your world gen:
        // Return [0..1] habitat score for colony bases (1 = excellent).
        std::function<float(int,int)> habitat_score = {};
        // Return true if a tile/position is blocked for a faction base.
        std::function<bool(int,int)> is_blocked = {};
    };

    // Generated set, including a simple symmetric relation matrix in [-1..1]
    struct FactionSet
    {
        std::vector<Faction> factions;    // size N
        std::vector<float>   relations;   // row-major N*N, [-1..1]
        int N() const { return static_cast<int>(factions.size()); }
        float relation(int i, int j) const { return relations[i*N()+j]; }
    };

    class FactionGenerator
    {
    public:
        // Core entry point.
        FactionSet generate(const FactionGenParams& params);

        // Deterministic sub-seed derivation: same world -> same factions.
        static uint64_t sub_seed(uint64_t world_seed, std::string_view tag);

        // Lightweight ethos compatibility used to seed relations.
        static float ethos_affinity(Ethos a, Ethos b);
    };
} // namespace colony
