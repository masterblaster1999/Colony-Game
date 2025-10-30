#pragma once
#include <string>
#include <cstdint>

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
    // No SDL — default constructor/destructor
    Game();
    ~Game();

    // Runs the main loop until quit. Returns exit code.
    int run();

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
    GameOptions opts_{};

    bool paused_ = false;
};
