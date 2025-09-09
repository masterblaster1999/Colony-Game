#pragma once
#include <string>
#include <cstdint>

// Forward-declare SDL types (no SDL headers in the header file)
struct SDL_Window;
struct SDL_Renderer;

// Game bootstrap options passed from the launcher
struct GameOptions {
    int         width        = 1280;
    int         height       = 720;
    bool        vsync        = true;
    bool        fullscreen   = false;
    bool        safeMode     = false;
    uint64_t    seed         = 0;
    std::string profile      = "default";
    std::string saveDir;     // e.g., ~/AppData/.../Saves
    std::string assetsDir;   // e.g., ./assets (not required in this build)
};

class Game {
public:
    Game(SDL_Window* window, SDL_Renderer* renderer, const GameOptions& opts);
    // Runs the main loop until quit. Returns exit code.
    int Run();

private:
    // Non-copyable
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // PIMPL is overkill; we keep implementation in Game.cpp.
    SDL_Window*   window_;
    SDL_Renderer* renderer_;
    GameOptions   opts_;
};
