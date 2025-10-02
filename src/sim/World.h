#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

// --- MOVE FROM GAME.CPP: enum class TileType { ... };
/*
enum class TileType : uint8_t {
    // paste your exact TileType here (no changes)
};
*/

// --- MOVE FROM GAME.CPP: struct Tile { ... }; (if you have one)
/*
struct Tile {
    // paste your exact members and helpers
};
*/

// --- MOVE FROM GAME.CPP: struct World { ... } (map data, rng seed, helpers)
/*
struct World {
    // paste your existing world state (tiles, size, rng, etc.)
};
*/

// --- MOVE FROM GAME.CPP: world generation & helpers
// Keep signatures exactly as they are today (so call sites don’t change).
/*
void worldGenerate(World& w, uint32_t seed);
bool worldInBounds(const World& w, int x, int y);
TileType worldGet(const World& w, int x, int y);
void worldSet(World& w, int x, int y, TileType tt);
// any mining/building/terrain-cost helpers you already have
*/
