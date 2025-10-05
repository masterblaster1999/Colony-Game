#pragma once
#include "FactionGen.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace colony
{
    inline Ethos ethos_from_string(const std::string& s)
    {
        if (s=="Traders")  return Ethos::Traders;
        if (s=="Raiders")  return Ethos::Raiders;
        if (s=="Settlers") return Ethos::Settlers;
        if (s=="Nomads")   return Ethos::Nomads;
        if (s=="Scholars") return Ethos::Scholars;
        if (s=="Cultists") return Ethos::Cultists;
        return Ethos::Settlers;
    }

    inline FactionGenParams load_faction_params(const std::filesystem::path& jsonPath,
                                                uint64_t worldSeed,
                                                int mapW, int mapH)
    {
        using json = nlohmann::json;
        std::ifstream f(jsonPath);
        if (!f.is_open()) throw std::runtime_error("Could not open " + jsonPath.string());
        json J; f >> J;

        FactionGenParams P;
        P.world_seed       = worldSeed;
        P.map_width        = mapW;
        P.map_height       = mapH;
        P.min_factions     = J.value("minFactions", 3);
        P.max_factions     = J.value("maxFactions", 6);
        P.min_base_spacing = J.value("minBaseSpacing", 128);

        // Palette
        if (J.contains("palette"))
        {
            for (auto& c : J["palette"])
            {
                std::array<uint8_t,3> rgb{
                    (uint8_t)c.at(0).get<int>(),
                    (uint8_t)c.at(1).get<int>(),
                    (uint8_t)c.at(2).get<int>()
                };
                P.palette.push_back(rgb);
            }
        }

        // Archetypes
        for (auto& a : J["archetypes"])
        {
            FactionArchetype A;
            A.id     = a.value("id", "unknown");
            A.ethos  = ethos_from_string(a.value("ethos", "Settlers"));
            A.weight = a.value("weight", 1.0f);
            auto t   = a.value("tech",        std::vector<float>{0.2f,0.8f});
            auto g   = a.value("aggression",  std::vector<float>{0.1f,0.9f});
            auto h   = a.value("hospitality", std::vector<float>{0.1f,0.9f});
            A.tech_min = t[0];        A.tech_max = t[1];
            A.aggression_min = g[0];  A.aggression_max = g[1];
            A.hospitality_min = h[0]; A.hospitality_max = h[1];

            P.archetypes.push_back(A);
        }

        return P;
    }
}
