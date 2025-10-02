// Slimmed "loop + wiring" implementation for Windows (SDL2 front-end).
// The simulation (world/economy/agents) and UI (camera/HUD) live in sim/* and ui/*.
//
// Main loop = fixed timestep with interpolation (see Gaffer on Games).
// - Deterministic updates at 60 Hz
// - Bounded steps per frame to avoid spiral-of-death
// - Alpha (accumulator fraction) reserved for render interpolation if you add it later

#include "game/Game.h"

#include "sim/World.h"
#include "sim/Economy.h"
#include "sim/Colonist.h"
#include "sim/Hostile.h"

#include "ui/Camera.h"
#include "ui/Hud.h"

#include <SDL.h>
#include <chrono>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace {

// Fixed simulation rate
constexpr double kSimHz                 = 60.0;
constexpr double kDtSeconds             = 1.0 / kSimHz;
constexpr int    kMaxStepsPerFrame      = 8;    // clamp catch-up
constexpr Uint8  kClearR = 0, kClearG = 0, kClearB = 0, kClearA = 255;

// Simple RAII for SDL renderer state if you ever add render targets later
struct FrameGuard {
    SDL_Renderer* r{};
    explicit FrameGuard(SDL_Renderer* ren) : r(ren) {}
    ~FrameGuard() = default;
};

static inline void set_initial_sdl_hints()
{
    // Nice default scaling and click-through when the window regains focus.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
}

} // namespace

// ---- Game wiring (constructor/destructor) -----------------------------------

Game::Game(SDL_Window* win, SDL_Renderer* ren)
    : window_(win),
      renderer_(ren),
      paused_(false),
      running_(false),
      lastFrameSec_(0.0)
{
    set_initial_sdl_hints();

    // Initialize simulation state
    // World/colony sizes, tiles, etc. should be defined in your sim/World.h
    const uint32_t seed = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    worldGenerate(world_, seed);
    colonyInit(colony_);

    // Optional: if your Camera has an init helper, feed it the viewport size.
    // We query the renderer so this file doesn’t need to know window opts.
    int vpW = 0, vpH = 0;
    if (renderer_) {
        SDL_GetRendererOutputSize(renderer_, &vpW, &vpH);
    }
    // If you exposed an initializer in ui/Camera.h, call it here:
    // cameraInit(camera_, vpW, vpH, world_.width, world_.height);
}

Game::~Game() = default;

// ---- Main loop --------------------------------------------------------------

int Game::run()
{
    using clock      = std::chrono::steady_clock;
    using seconds_d  = std::chrono::duration<double>;

    running_ = true;

    const seconds_d dt{kDtSeconds};
    seconds_d accumulator{0.0};
    auto t0 = clock::now();

    while (running_) {
        // --- frame timing
        const auto t1 = clock::now();
        const auto frame = t1 - t0;
        t0 = t1;
        lastFrameSec_ = std::chrono::duration<double>(frame).count();
        accumulator  += std::chrono::duration_cast<seconds_d>(frame);

        // --- events / input (keep it here so sim sees latest decisions)
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running_ = false;
                } else if (ev.key.keysym.scancode == SDL_SCANCODE_P) {
                    paused_ = !paused_;
                }
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    // Pause simulation on focus loss; keep rendering.
                    paused_ = true;
                }
                break;
            default: break;
            }
        }

        // --- fixed-step simulation
        int steps = 0;
        if (!paused_) {
            while (accumulator >= dt) {
                // Order: colony systems -> agents -> hostiles -> camera
                colonyUpdate(colony_, kDtSeconds);
                colonistsUpdate(colonists_, world_, colony_, kDtSeconds);
                hostilesUpdate(hostiles_, world_, colony_, kDtSeconds);
                cameraUpdate(camera_, kDtSeconds);

                accumulator -= dt;
                if (++steps >= kMaxStepsPerFrame) {
                    // Drop excess time if the machine is struggling.
                    accumulator = seconds_d::zero();
                    break;
                }
            }
        } else {
            // If paused, don't accumulate unbounded time debt.
            accumulator = seconds_d::zero();
        }

        // Interpolation factor for rendering between fixed ticks, if desired.
        const double alpha = (dt.count() > 0.0) ? (accumulator.count() / dt.count()) : 0.0;
        (void)alpha; // pass to your renderer if/when you add visual interpolation

        // --- render frame
        if (renderer_) {
            FrameGuard guard(renderer_);
            SDL_SetRenderDrawColor(renderer_, kClearR, kClearG, kClearB, kClearA);
            SDL_RenderClear(renderer_);

            // You can move world rendering into a ui::Renderer later.
            // HUD should stay read-only: it inspects World/Colony/Camera only.
            hudRender(renderer_, world_, colony_, camera_);

            SDL_RenderPresent(renderer_);
        }
    }

    return 0;
}
