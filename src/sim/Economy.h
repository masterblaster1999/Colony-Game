#pragma once
#include <cstdint>
#include <vector>
#include <string>

// --- MOVE FROM GAME.CPP: resource/building defs
/*
enum class Resource : uint8_t { ... };
enum class Building : uint8_t { ... };
*/

// --- MOVE FROM GAME.CPP: stockpile/colony structs & helpers
/*
struct Stockpile {
    // counts, add/remove helpers
};

struct Colony {
    Stockpile stock;
    // buildings list, jobs queues, etc.
};
*/

// --- MOVE FROM GAME.CPP: economy helpers
/*
void colonyInit(Colony& c);
void colonyUpdate(Colony& c, double dt);
const char* toString(Resource r);
const char* toString(Building b);
*/
