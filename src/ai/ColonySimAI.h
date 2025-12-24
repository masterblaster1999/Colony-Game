// src/ColonySimAI.hpp
// MIT License (c) 2025 YourName or masterblaster1999
//
// Single-file colony-sim core:
// - Grid + tiles (costs, doors, reservations, zones)
// - A* pathfinding (+ optional JPS toggle), terrain costs, dynamic obstacles, path cache
// - Event bus
// - Items, inventories, ground items, stockpile zones
// - Job system (mine/chop/haul/build/farm + craft/cook/research/heal/train/tame/patrol/trade)
// - Colonists (skills, schedules, needs), greedy job assignment
// - Header-only GOAP-ish planner (actions with preconditions/effects → sequences of jobs)
// - Workstations & recipes (sawmill/kitchen/research bench/forge) + auto job spawner
// - Persistence (save/load) & replay trace
// - Debug ASCII overlay renderers
//
// Usage (minimal):
//   #include "ColonySimAI.hpp"
//   colony::World world(96, 64);
//   colony::JobQueue jobs;
//   world.spawnColonist({3,3});
//   jobs.push(colony::Job::Chop({10,7}));
//   while (running) world.update(dt, jobs);
//
// Compile: C++17+ (header-only). No external deps.

#pragma once

// ------------------------------ Includes ------------------------------
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ------------------------------ Namespace ------------------------------
namespace colony {

// ------------------------------ Config toggles ------------------------------
#ifndef COLONY_SIM_ENABLE_JPS
#define COLONY_SIM_ENABLE_JPS 1     // Jump Point Search pruning (simple impl)
#endif
#ifndef COLONY_SIM_PATHCACHE_MAX
#define COLONY_SIM_PATHCACHE_MAX 4096
#endif
#ifndef COLONY_SIM_DEBUG
#define COLONY_SIM_DEBUG 0
#endif


// NOTE: This header was split into smaller *.inl parts under src/ai/detail/
//       to improve readability while remaining header-only.

#include "detail/ColonySimAI_Utilities.inl"
#include "detail/ColonySimAI_ItemsInventory.inl"
#include "detail/ColonySimAI_GridTiles.inl"
#include "detail/ColonySimAI_StockpilesGround.inl"
#include "detail/ColonySimAI_Jobs.inl"
#include "detail/ColonySimAI_EventBus.inl"
#include "detail/ColonySimAI_Pathfinding.inl"
#include "detail/ColonySimAI_Agents.inl"
#include "detail/ColonySimAI_WorkstationsRecipes.inl"
#include "detail/ColonySimAI_Actions.inl"
#include "detail/ColonySimAI_JobQueue.inl"
#include "detail/ColonySimAI_WorldOrchestrator.inl"
