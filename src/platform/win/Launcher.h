// Launcher.h
#pragma once
int RunLauncher(); // parses args, loads config, starts renderer/game loop

// Launcher.cpp
#include <filesystem>
#include <string>
// parse args, find assets under /assets, load config, call StartGame(assetsDir)
int RunLauncher() {
    const auto assets = std::filesystem::current_path() / "assets";
    // ... validate assets exist; log helpful errors before returning
    return StartGame(assets); // your existing engine entry call
}
