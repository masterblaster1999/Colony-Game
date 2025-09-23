// src/proc/WorldSeed.h
#pragma once
#include <cstdint>
struct WorldSeed {
    uint64_t world = 0xC01oNYULL; // CLI or savegame populated
    uint64_t terrain()   const { return world ^ 0x9E37'79B9'7F4A'7C15ULL; }
    uint64_t climate()   const { return world ^ 0xBF58'476D'1CE4'E5B9ULL; }
    uint64_t foliage()   const { return world ^ 0x94D0'49BB'1331'11EBULL; }
    uint64_t ambAudio()  const { return world ^ 0x8C67'AA9B'07FE'98F1ULL; }
    uint64_t names()     const { return world ^ 0xD134'2543'DE82'EF95ULL; }
};
