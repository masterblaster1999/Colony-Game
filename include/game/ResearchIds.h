// include/game/ResearchIds.h
#pragma once
#include <cstdint>

enum class ResearchId : uint8_t
{
    None = 0,

    SolarEfficiency,
    HabitatLifeSupport,
    OxygenRecycling,
    DustStormShielding,
    AdvancedMining,

    Count
};
