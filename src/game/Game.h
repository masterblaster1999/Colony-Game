#pragma once
#include <string>
#include <cstdint>

// Forward-declare SDL types (avoid including heavy SDL headers here)
struct SDL_Window;
struct SDL_Renderer;

// ----------------------------------------------------------------------------
// Game bootstrap options passed from the launcher
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Game main class (manages core loop, simulation, and rendering)
// ----------------------------------------------------------------------------
class Game {
public:
    Game(SDL_Window* window, SDL_Renderer* renderer, const GameOptions& opts);

    // Runs the main loop until quit. Returns exit code.
    int Run();

    // ------------------------------------------------------------------------
    // Runtime state helpers
    // ------------------------------------------------------------------------
    inline bool IsPaused() const noexcept { return paused_; }
    inline void SetPaused(bool v) noexcept { paused_ = v; }
    inline void TogglePause() noexcept { paused_ = !paused_; }

private:
    // Non-copyable
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // ------------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------------
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    GameOptions   opts_{};

    // Fix: previously undeclared variable causing C2065
    bool paused_ = false;
};
