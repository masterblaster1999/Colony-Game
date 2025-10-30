// Windows/D3D path "loop + wiring" implementation (no SDL).
// The simulation (world/economy/agents) and UI (camera/HUD) live in sim/* and ui/*.
//
// Main loop = fixed timestep with interpolation (see Gaffer on Games).
// - Deterministic updates at 60 Hz
// - Bounded steps per frame to avoid spiral-of-death
// - Alpha (accumulator fraction) reserved for render interpolation if you add it later

#include "game/Game.h"

// --- Game systems (new wiring) -----------------------------------------------
#include "game/GameSystems_Simulation.h"
#include "game/GameSystems_Input.h"
#include "game/GameSystems_Render.h"

// --- Simulation & UI state ---------------------------------------------------
#include "sim/World.h"
#include "sim/Economy.h"
#include "sim/Colonist.h"
#include "sim/Hostile.h"
#include "ui/Camera.h"
#include "ui/Hud.h"

// --- Windows message pump (no SDL) ------------------------------------------
// These guards avoid macro redefinition warnings if your build already defines
// WIN32_LEAN_AND_MEAN/NOMINMAX on the command line (preferred).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

// --- Standard library --------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <algorithm>

// Anonymous namespace for loop constants
namespace {

// Fixed simulation rate
constexpr double kSimHz            = 60.0;
constexpr double kDtSeconds        = 1.0 / kSimHz;
constexpr int    kMaxStepsPerFrame = 8;   // clamp catch-up

using Clock     = std::chrono::steady_clock;   // monotonic clock
using seconds_d = std::chrono::duration<double>;

// Basic keyboard helpers (handles both normal and "system" keydowns like Alt+F4)
static inline bool is_keydown(const MSG& msg, WPARAM vk) {
    const bool kd  = (msg.message == WM_KEYDOWN) || (msg.message == WM_SYSKEYDOWN);
    return kd && (msg.wParam == vk);
}

} // namespace

// ---- Game wiring (constructor/destructor) -----------------------------------

Game::Game()  = default;
Game::~Game() = default;

// ---- Main loop --------------------------------------------------------------
//
// NOTE: This function intentionally avoids storing/using SDL members and does
// not own D3D objects. Rendering is routed via your D3D render path (e.g. a
// renderer system) that should already be integrated elsewhere.
//
// If your Game.h declares a different return type for run(), adjust just
// the signature below to match. Everything else is header-agnostic.
int Game::run()
{
    // --- Runtime flags (kept local to avoid mismatching private members) -----
    bool   running       = true;
    bool   paused        = false;
    double lastFrameSec  = 0.0;

    // --- Simulation state (lifetime = whole run) -----------------------------
    // These types/functions come from your existing sim/ui modules.
    // If you split them differently, just keep the init/update order.
    World     world{};
    Colony    colony{};
    Colonists colonists{};
    Hostiles  hostiles{};
    Camera    camera{};

    // Initialize simulation state via the new GameSystems_* layer
    const uint32_t seed = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    GameSystems::Simulation::Init(world, colony, colonists, hostiles, seed);

    // --- Fixed-step loop setup -----------------------------------------------
    const seconds_d dt{kDtSeconds};
    seconds_d accumulator{0.0};
    auto t0 = Clock::now();

    // --- Win32 message struct (non-blocking) ---------------------------------
    MSG msg{};
    while (running) {
        // --- frame timing
        const auto t1    = Clock::now();
        const auto frame = t1 - t0;
        t0               = t1;
        lastFrameSec     = std::chrono::duration<double>(frame).count();
        accumulator     += std::chrono::duration_cast<seconds_d>(frame);

        // --- process OS messages (do not block the loop)
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            // Minimal hotkeys (Esc: quit, P: pause toggle).
            if (is_keydown(msg, VK_ESCAPE)) {
                running = false;
            } else if (is_keydown(msg, 'P')) {
                paused = !paused;
            } else if (msg.message == WM_SETFOCUS) {
                // Optional: unpause on focus.
                // paused = false;
            } else if (msg.message == WM_KILLFOCUS) {
                // Pause simulation when focus is lost.
                paused = true;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // --- fixed-step simulation
        int steps = 0;
        if (!paused) {
            while (accumulator >= dt) {
                // Drive the new systems instead of legacy free functions
                GameSystems::Simulation::Update(world, colony, colonists, hostiles, kDtSeconds);
                GameSystems::Input::Update(camera, kDtSeconds);

                accumulator -= dt;
                if (++steps >= kMaxStepsPerFrame) {
                    // Drop excess time if machine is struggling (spiral prevention).
                    accumulator = seconds_d::zero();
                    break;
                }
            }
        } else {
            // If paused, do not accumulate unbounded time debt and don't spin a core.
            accumulator = seconds_d::zero();
            ::Sleep(1); // tiny yield while paused to reduce CPU burn
        }

        // Interpolation factor (for render interpolation if desired).
        const double alpha = (dt.count() > 0.0)
                           ? (accumulator.count() / dt.count())
                           : 0.0;
        (void)alpha; // pass to your renderer if/when you add visual interpolation

        // --- render frame via your D3D path
        // Typical place to invoke your render systems:
        // GameSystems::Render::Frame(world, colony, camera, alpha);
        //
        // If your Render system needs a context (device/swapchain), thread that through here.
    }

    return 0;
}
