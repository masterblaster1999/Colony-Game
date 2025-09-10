// src/paths.h
#pragma once
#include <filesystem>

namespace paths {

// Returns the directory containing the running executable.
std::filesystem::path exe_dir();

// Returns a directory that contains your game data.  Searches common spots:
//   1) alongside the exe: <exe>/assets   (or resources/data)
//   2) portable installs: <exe>/../share/Colony-Game
//   3) dev builds:        <source>/assets copied next to the exe
//   4) override:          env COLONY_GAME_ASSETS
// Returns empty() if nothing sensible is found.
std::filesystem::path find_assets_root();

// Convenience join: assets("textures/atlas.png") -> <assets_root>/textures/atlas.png
std::filesystem::path assets(const std::filesystem::path& rel);
}
