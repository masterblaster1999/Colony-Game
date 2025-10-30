#pragma once

namespace colony::game {

// Forward decls to avoid heavy includes:
struct Game;
struct GameThreads;
struct RenderRegistry; // or whatever type your registry is

// Declare what Game.cpp needs. Match the .cpp exactly:
void RegisterRenderSystems(Game& g, GameThreads& gt, RenderRegistry& r);

} // namespace colony::game
