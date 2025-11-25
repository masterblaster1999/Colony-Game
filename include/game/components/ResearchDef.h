// include/game/ResearchDef.h
#pragma once
#include "ResearchIds.h"
#include <string>
#include <vector>

struct ResearchDef
{
    ResearchId id;
    const char* key;         // "solar_efficiency" for save/JSON
    const char* name;        // "Solar Efficiency I"
    const char* description; // short UI text

    int scienceCost;         // total research points required
    std::vector<ResearchId> prereqs;
};
