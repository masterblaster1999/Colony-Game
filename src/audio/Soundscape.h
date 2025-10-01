#pragma once
// Soundscape.h - procedural ambient generator for Colony-Game
// Requires: C++17, miniaudio (vendored)

// third_party/miniaudio/miniaudio.h should be on the include path
#include "miniaudio.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <cstdint>

// -----------------------------
// Public API and data contracts
// -----------------------------

namespace audio {

enum class Biome : uint8_t {
    Forest, Desert, Snow, Swamp, Plains, Mountain, Ocean, Caves,
    Count
};

enum class DayPhase : uint8_t { Dawn, Day, Dusk, Night, Count };

enum class Weather : uint8_t {
    Clear, Rain, HeavyRain, Storm, Snowfall, Windy, Fog,
    Count
};

struct WorldState {
    Biome   biome            = Biome::Plains;
    DayPhase dayPhase        = DayPhase::Day;
    Weather weather          = Weather::Clear;

    // Optional continuous params in [0..1]:
    float   windIntensity    = 0.0f;   // 0 calm -> 1 stormy (used to select wind loop)
    float   humidityOrWater  = 0.0f;   // 0 dry  -> 1 watery (encourages water bed)
    float   dangerLevel      = 0.0f;   // 0 calm -> 1 high danger (brings in tension bed)

    bool    isIndoors        = false;  // can be used later to attenuate high-freq, etc.
    float   timeOfDayHours   = 12.0f;  // optional (0..24); used only if you want auto dayPhase
};

struct InitParams {
    std::string assetRoot = "res/audio/ambient"; // folder described in the README above
    // You can add sampleRate/channels overrides if you want; miniaudio picks sensible defaults.
};

class Soundscape {
public:
    Soundscape() = default;
    ~Soundscape();

    // Initialize engine + groups and prewarm RNG.
    bool init(const InitParams& params);

    // Release all groups/sounds/engine.
    void shutdown();

    // Update & drive procedural selection each frame.
    // dtSeconds: frame delta time.
    void update(const WorldState& state, float dtSeconds);

    // Master gain in dB (applied on the master group).
    void setMasterVolumeDb(float db);

    // Category toggles (these just set group volumes; cheap).
    void muteAll(bool mute);
    void muteWildlife(bool mute);
    void muteWeather(bool mute);

private:
    Soundscape(const Soundscape&) = delete;
    Soundscape& operator=(const Soundscape&) = delete;

    // Internal helpers and structures
public: // Make Category usable from Soundscape.cpp and other TUs
    enum class Category : uint8_t { Bed, Wind, Water, Wildlife, Rain, Event, Thunder, Count };

private:
    struct Group {
        ma_sound_group handle{};
        bool initialized = false;
    };

    struct LoopSlot {
        // Active loop instance:
        ma_sound current{};
        bool     curInit = false;
        std::string curPath;

        // Pending loop instance (during crossfade):
        ma_sound next{};
        bool     nextInit = false;
        std::string nextPath;

        // Timing for swap:
        float crossfadeMsRemaining = 0.0f;

        // Group this slot routes into:
        Group* group = nullptr;

        // Utility:
        void uninitCurrent();
        void uninitNext();
    };

    struct OneShotSchedule {
        // For intermittent SFX (bird calls, thunder, etc.)
        float timer = 0.0f;
        float minDelay = 4.0f;
        float maxDelay = 12.0f;
        Category category = Category::Wildlife;
        std::vector<std::string> candidates;
        Group* group = nullptr;
        float gain = 1.0f; // linear
        bool enabled = true;
    };

    // --- core
    bool initEngine_();
    void uninitEngine_();

    void initGroups_();
    void uninitGroups_();

    void ensureLoop_(Category cat, const std::string& path, float targetVolLinear, int fadeMs);
    void updateCrossfades_(float dtMs);
    void setGroupVolLinear_(Category cat, float vol);
    float dbToLinear_(float db) const;

    // Procedural selection
    std::string chooseBedLoop_(const WorldState& s) const;
    std::string chooseWindLoop_(const WorldState& s) const;
    std::string chooseWaterLoop_(const WorldState& s) const;
    std::string chooseWildlifeLoop_(const WorldState& s) const;
    std::string chooseRainLoop_(const WorldState& s) const;
    std::string chooseEventLoop_(const WorldState& s) const;
    std::string chooseThunderOneShot_(const WorldState& s) const;

    // Scheduling one-shots:
    void updateOneShots_(const WorldState& s, float dtSeconds);

private:
    InitParams params_;

    ma_engine engine_{};
    bool engineInit_ = false;

    // Sound groups (buses)
    Group gMaster_{};
    Group gBed_{}, gWind_{}, gWater_{}, gWildlife_{}, gRain_{}, gEvent_{}, gThunder_{};

    // Per-category loop slot
    LoopSlot slots_[(size_t)Category::Count]{};

    // One-shot schedulers (birds day, frogs at night in swamp, thunder in storm)
    OneShotSchedule birdsDay_;
    OneShotSchedule frogsNight_;
    OneShotSchedule wolvesNight_;
    OneShotSchedule thunder_;

    // RNG (used by const selection helpers; distributions require non-const URNG references)
    mutable std::mt19937 rng_{std::random_device{}()};

    // Last frame state to detect changes
    WorldState lastState_{};
    bool firstFrame_ = true;
};

} // namespace audio
