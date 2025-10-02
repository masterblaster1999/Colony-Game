#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Forward declare World and Colony so header stays light:
struct World;
struct Colony;

// --- MOVE FROM GAME.CPP: Colonist data/behavior
/*
struct Colonist {
    // position, target, state machine fields, path cache, cooldowns...
};
*/

// --- MOVE FROM GAME.CPP: per-agent + batch updates
/*
void colonistUpdate(Colonist& c, World& w, Colony& col, double dt);
void colonistsUpdate(std::vector<Colonist>& cs, World& w, Colony& col, double dt);
*/
