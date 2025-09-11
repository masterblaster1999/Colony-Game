// ──────────────────────────────────────────────────────────────────────────────
// GameAPI.h  —  Stable launcher ⇄ game boundary for Colony-Game
// Place at: src/game/GameAPI.h
//
// This header centralizes the contract used by both the launcher and the game.
// It preserves the original API:
//      struct GameOptions;
//      int RunColonyGame(const GameOptions&);
// …and layers on:
//   • Clear sub-config structs (display, graphics, audio, gameplay, etc.)
//   • Safe defaults via in-class initializers
//   • Header-only validation & sanitization helpers
//   • Optional export macro for DLL builds (no-op for static builds)
//   • Optional C ABI shim (off by default)
// ──────────────────────────────────────────────────────────────────────────────

#pragma once

// Require C++17 or later.
#if !defined(__cplusplus) || (__cplusplus < 201703L)
#  error "GameAPI.h requires C++17 or newer."
#endif

#include <cstdint>
#include <string>
#include <algorithm>   // std::clamp
#include <sstream>     // ToString helper (optional)

// ============ Export Macros (safe no-op for static builds) ====================
#if defined(_WIN32) && defined(COLONY_GAMEAPI_DLL)
#  ifdef COLONY_GAMEAPI_BUILD
#    define COLONY_GAMEAPI_EXPORT __declspec(dllexport)
#  else
#    define COLONY_GAMEAPI_EXPORT __declspec(dllimport)
#  endif
#else
#  define COLONY_GAMEAPI_EXPORT
#endif

// ========================= Constants & Enums ==================================
namespace GameAPI {

constexpr int   kMinWidth        = 640;
constexpr int   kMinHeight       = 360;
constexpr int   kMaxWidth        = 8192;
constexpr int   kMaxHeight       = 8192;
constexpr int   kDefaultWidth    = 1280;
constexpr int   kDefaultHeight   = 720;
constexpr int   kDefaultTargetFPS= 60;

enum class WindowMode : std::uint8_t {
    Windowed = 0,
    BorderlessWindow,
    FullscreenExclusive
};

enum class VSyncMode : std::uint8_t {
    Off = 0,
    On,
    Adaptive // where supported; otherwise treated as On
};

enum class RendererBackend : std::uint8_t {
    Auto = 0,
    OpenGL,
    Vulkan,
    Direct3D11,
    Direct3D12,
    Metal
};

} // namespace GameAPI

// ============================== Sub-configs ===================================
struct DisplayOptions {
    int                         width       = GameAPI::kDefaultWidth;
    int                         height      = GameAPI::kDefaultHeight;
    int                         refreshHz   = 0;  // 0 = don't care / use display default
    GameAPI::WindowMode         mode        = GameAPI::WindowMode::Windowed;
    GameAPI::VSyncMode          vsync       = GameAPI::VSyncMode::On;
    bool                        resizable   = true;
    bool                        allowHiDPI  = true; // macOS/Windows high-DPI awareness

    inline void Sanitize() {
        width  = std::clamp(width,  GameAPI::kMinWidth,  GameAPI::kMaxWidth);
        height = std::clamp(height, GameAPI::kMinHeight, GameAPI::kMaxHeight);
        if (refreshHz < 0) refreshHz = 0;
    }

    inline bool IsValid(std::string* err = nullptr) const {
        if (width < GameAPI::kMinWidth || width > GameAPI::kMaxWidth) {
            if (err) *err = "DisplayOptions.width out of range.";
            return false;
        }
        if (height < GameAPI::kMinHeight || height > GameAPI::kMaxHeight) {
            if (err) *err = "DisplayOptions.height out of range.";
            return false;
        }
        return true;
    }
};

struct GraphicsOptions {
    GameAPI::RendererBackend    backend     = GameAPI::RendererBackend::Auto;
    int                         msaaSamples = 0;   // 0,2,4,8,16 (clamped)
    int                         anisotropy  = 1;   // 1..16 (clamped to power-of-two-ish range)
    bool                        tripleBuffer= false;
    bool                        debugGPU    = false; // enable validation layers if available

    inline void Sanitize() {
        // Clamp MSAA to common supported values.
        if (msaaSamples != 0 && msaaSamples != 2 && msaaSamples != 4 &&
            msaaSamples != 8 && msaaSamples != 16) {
            msaaSamples = 0;
        }
        anisotropy = std::clamp(anisotropy, 1, 16);
    }

    inline bool IsValid(std::string* err = nullptr) const {
        if (anisotropy < 1 || anisotropy > 16) {
            if (err) *err = "GraphicsOptions.anisotropy must be within [1,16].";
            return false;
        }
        return true;
    }
};

struct AudioOptions {
    float                       masterVolume = 1.0f; // [0,1]
    float                       musicVolume  = 0.8f; // [0,1]
    float                       sfxVolume    = 1.0f; // [0,1]
    bool                        mute         = false;

    inline void Sanitize() {
        auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
        masterVolume = clamp01(masterVolume);
        musicVolume  = clamp01(musicVolume);
        sfxVolume    = clamp01(sfxVolume);
    }

    inline bool IsValid(std::string* err = nullptr) const {
        auto in01 = [](float v) { return v >= 0.0f && v <= 1.0f; };
        if (!(in01(masterVolume) && in01(musicVolume) && in01(sfxVolume))) {
            if (err) *err = "AudioOptions volumes must be within [0,1].";
            return false;
        }
        return true;
    }
};

struct PerformanceBudget {
    int                         targetFPS       = GameAPI::kDefaultTargetFPS; // 0 = uncapped
    int                         maxWorkerThreads= 0; // 0 = auto (hardware_concurrency)

    inline void Sanitize() {
        if (targetFPS < 0) targetFPS = 0;
        if (maxWorkerThreads < 0) maxWorkerThreads = 0;
    }

    inline bool IsValid(std::string* err = nullptr) const {
        if (targetFPS < 0) {
            if (err) *err = "PerformanceBudget.targetFPS cannot be negative.";
            return false;
        }
        if (maxWorkerThreads < 0) {
            if (err) *err = "PerformanceBudget.maxWorkerThreads cannot be negative.";
            return false;
        }
        return true;
    }
};

struct GameplayOptions {
    std::uint64_t               seed        = 0;        // 0 = random seed chosen by game
    bool                        safeMode    = false;    // skip optional systems for stability
    std::string                 profile     = "default";
    std::string                 lang        = "en-US";

    inline void Sanitize() {
        // No numeric ranges to clamp; strings may be normalized by the game.
    }

    inline bool IsValid(std::string* /*err*/ = nullptr) const {
        return true;
    }
};

struct Paths {
    std::string                 assetsDir;              // e.g. "./assets"
    std::string                 saveDir;                // e.g. "%LOCALAPPDATA%/ColonyGame/Saves"
    std::string                 logsDir;                // optional explicit logs path

    inline void Sanitize() { /* leave as-is; game resolves empties to defaults */ }
    inline bool IsValid(std::string* /*err*/ = nullptr) const { return true; }
};

struct DebugOptions {
    bool                        verboseLogs     = false;
    bool                        traceEvents     = false; // instrumented profiling traces
    bool                        crashOnAssert   = false;
    bool                        headless        = false; // allow boot without a window (CI/tests)

    inline void Sanitize() {}
    inline bool IsValid(std::string* /*err*/ = nullptr) const { return true; }
};

// ============================== Aggregate =====================================
// The single options bag the launcher fills and the game consumes.
struct GameOptions {
    DisplayOptions      display {};
    GraphicsOptions     graphics{};
    AudioOptions        audio   {};
    PerformanceBudget   perf    {};
    GameplayOptions     gameplay{};
    Paths               paths   {};
    DebugOptions        debug   {};

    // Convenience: sanitize every sub-config.
    inline void Sanitize() {
        display.Sanitize();
        graphics.Sanitize();
        audio.Sanitize();
        perf.Sanitize();
        gameplay.Sanitize();
        paths.Sanitize();
        debug.Sanitize();
    }

    // Validate every sub-config. Returns true on success; false and sets `err` on failure.
    inline bool IsValid(std::string* err = nullptr) const {
        if (!display.IsValid(err))  return false;
        if (!graphics.IsValid(err)) return false;
        if (!audio.IsValid(err))    return false;
        if (!perf.IsValid(err))     return false;
        if (!gameplay.IsValid(err)) return false;
        if (!paths.IsValid(err))    return false;
        if (!debug.IsValid(err))    return false;
        return true;
    }

    // Helpful for logging/debugging from the launcher.
    inline std::string ToString() const {
        std::ostringstream ss;
        ss  << "Display{ " << display.width << "x" << display.height
            << ", Hz=" << display.refreshHz
            << ", mode=" << static_cast<int>(display.mode)
            << ", vsync="<< static_cast<int>(display.vsync)
            << " } "
            << "Graphics{ backend=" << static_cast<int>(graphics.backend)
            << ", MSAA=" << graphics.msaaSamples
            << ", AF="   << graphics.anisotropy
            << ", triple="<< (graphics.tripleBuffer ? "Y" : "N")
            << " } "
            << "Audio{ M=" << audio.masterVolume
            << ", BGM=" << audio.musicVolume
            << ", SFX=" << audio.sfxVolume
            << ", mute=" << (audio.mute ? "Y" : "N") << " } "
            << "Perf{ targetFPS=" << perf.targetFPS
            << ", maxThreads=" << perf.maxWorkerThreads << " } "
            << "Gameplay{ seed=" << gameplay.seed
            << ", safe=" << (gameplay.safeMode ? "Y" : "N")
            << ", profile=\"" << gameplay.profile << "\""
            << ", lang=\"" << gameplay.lang << "\" } "
            << "Paths{ assets=\"" << paths.assetsDir
            << "\", saves=\"" << paths.saveDir
            << "\", logs=\"" << paths.logsDir << "\" } "
            << "Debug{ verbose=" << (debug.verboseLogs ? "Y" : "N")
            << ", trace=" << (debug.traceEvents ? "Y" : "N")
            << ", crashOnAssert=" << (debug.crashOnAssert ? "Y" : "N")
            << ", headless=" << (debug.headless ? "Y" : "N") << " }";
        return ss.str();
    }
};

// ============================== Entry Points ==================================
// The game implements this function in its core code (unchanged signature).
COLONY_GAMEAPI_EXPORT int RunColonyGame(const GameOptions&);

// ---------- Optional C ABI shim (header-only wrapper). ------------------------
// Enable if you want a plain C launcher or scripting host to call into the game.
// This does NOT change the main C++ ABI; it simply exposes an additional symbol.
//
// Usage (build system): add -DCOLONY_GAMEAPI_EXPOSE_C to the launcher *and* core.
#if defined(COLONY_GAMEAPI_EXPOSE_C)
extern "C" COLONY_GAMEAPI_EXPORT inline int RunColonyGame_C(const GameOptions* opts) {
    return RunColonyGame(*opts);
}
#endif

// ============================== Helper Utilities ==============================
// Quick preflight that launchers can call before booting the game.
inline bool ValidateGameOptions(const GameOptions& opt, std::string* err = nullptr) {
    return opt.IsValid(err);
}

// Apply safe clamping across all sub-configs (call after parsing CLI/env).
inline void SanitizeGameOptions(GameOptions& opt) {
    opt.Sanitize();
}

// Detect whether we’re likely in a debug build (useful for setting defaults).
constexpr bool GameApiIsDebugBuild() {
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}
