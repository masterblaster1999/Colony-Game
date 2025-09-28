// Soundscape.cpp
#include "Soundscape.h"

#include <algorithm>
#include <iostream>
#include <random>

namespace audio {

using Cat = Soundscape::Category;

// --------------------------------------
// Helpers
// --------------------------------------
static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

static const int kDefaultCrossfadeMs = 1200;

// Asset table – edit these to match your filenames/layout
struct Paths {
    const char* assetRoot = "res/audio/ambient";

    // Bed per biome/time
    const char* bedForestDay   = "bed/forest_day.ogg";
    const char* bedForestNight = "bed/forest_night.ogg";
    const char* bedDesertDay   = "bed/desert_day.ogg";
    const char* bedDesertNight = "bed/desert_night.ogg";
    const char* bedPlainsDay   = "bed/plains_day.ogg";
    const char* bedPlainsNight = "bed/plains_night.ogg";
    const char* bedSnowDay     = "bed/snow_day.ogg";
    const char* bedSnowNight   = "bed/snow_night.ogg";
    const char* bedSwampNight  = "bed/swamp_night.ogg";
    const char* bedMountainDay = "bed/mountain_day.ogg";
    const char* bedOceanDay    = "bed/ocean_day.ogg";
    const char* bedCavesNight  = "bed/caves_night.ogg";

    // Wind loops
    const char* windLight  = "wind/light_loop.ogg";
    const char* windMed    = "wind/medium_loop.ogg";
    const char* windHeavy  = "wind/heavy_loop.ogg";

    // Water loops
    const char* waterStream = "water/stream_loop.ogg";
    const char* waterCoast  = "water/coast_loop.ogg";
    const char* waterSwamp  = "water/swamp_loop.ogg";

    // Wildlife loops
    const char* birdsDay1   = "wildlife/birds_day_01.ogg";
    const char* birdsDay2   = "wildlife/birds_day_02.ogg";
    const char* crickets    = "wildlife/crickets_night_01.ogg";
    const char* frogs       = "wildlife/frogs_swamp_night_01.ogg";
    const char* wolves      = "wildlife/wolves_snow_night_01.ogg";

    // Rain + storm
    const char* rainLight   = "rain/light_loop.ogg";
    const char* rainHeavy   = "rain/heavy_loop.ogg";
    const char* stormBed    = "rain/storm_bed_loop.ogg";

    // Thunder one-shots
    std::vector<const char*> thunder = {
        "thunder/thunder_01.ogg",
        "thunder/thunder_02.ogg"
    };
};
static const Paths kPaths;

// Utility to join root + relative
static std::string pathJoin(const std::string& root, const char* rel) {
    if (root.empty()) return std::string(rel);
    if (root.back() == '/' || root.back() == '\\') return root + rel;
#ifdef _WIN32
    return root + "\\" + rel;
#else
    return root + "/" + rel;
#endif
}

// --------------------------------------
// LoopSlot uninit helpers
// --------------------------------------
void Soundscape::LoopSlot::uninitCurrent() {
    if (curInit) {
        ma_sound_stop(&current);
        ma_sound_uninit(&current);
        curInit = false;
        curPath.clear();
    }
}
void Soundscape::LoopSlot::uninitNext() {
    if (nextInit) {
        ma_sound_stop(&next);
        ma_sound_uninit(&next);
        nextInit = false;
        nextPath.clear();
    }
}

// --------------------------------------
// Engine / groups
// --------------------------------------
Soundscape::~Soundscape() { shutdown(); }

bool Soundscape::init(const InitParams& p) {
    params_ = p;
    if (!initEngine_()) return false;
    initGroups_();

    // route slots to groups
    slots_[(size_t)Cat::Bed].group      = &gBed_;
    slots_[(size_t)Cat::Wind].group     = &gWind_;
    slots_[(size_t)Cat::Water].group    = &gWater_;
    slots_[(size_t)Cat::Wildlife].group = &gWildlife_;
    slots_[(size_t)Cat::Rain].group     = &gRain_;
    slots_[(size_t)Cat::Event].group    = &gEvent_;
    slots_[(size_t)Cat::Thunder].group  = &gThunder_;

    // one-shot schedulers
    birdsDay_.category  = Cat::Wildlife;
    birdsDay_.group     = &gWildlife_;
    birdsDay_.candidates= { pathJoin(params_.assetRoot, kPaths.birdsDay1), pathJoin(params_.assetRoot, kPaths.birdsDay2) };
    birdsDay_.minDelay  = 6.0f; birdsDay_.maxDelay = 14.0f; birdsDay_.gain = 0.9f;

    frogsNight_.category= Cat::Wildlife;
    frogsNight_.group   = &gWildlife_;
    frogsNight_.candidates = { pathJoin(params_.assetRoot, kPaths.frogs) };
    frogsNight_.minDelay= 8.0f; frogsNight_.maxDelay = 16.0f; frogsNight_.gain = 0.8f;

    wolvesNight_.category= Cat::Wildlife;
    wolvesNight_.group   = &gWildlife_;
    wolvesNight_.candidates = { pathJoin(params_.assetRoot, kPaths.wolves) };
    wolvesNight_.minDelay= 12.0f; wolvesNight_.maxDelay = 24.0f; wolvesNight_.gain = 0.7f;

    thunder_.category   = Cat::Thunder;
    thunder_.group      = &gThunder_;
    for (auto* rel : kPaths.thunder) thunder_.candidates.emplace_back(pathJoin(params_.assetRoot, rel));
    thunder_.minDelay   = 6.0f; thunder_.maxDelay = 18.0f; thunder_.gain = 1.0f;

    // Start with muted rain/event/thunder groups; they ramp as conditions demand.
    ma_sound_group_set_volume(&gRain_.handle,     0.0f);
    ma_sound_group_set_volume(&gEvent_.handle,    0.0f);
    ma_sound_group_set_volume(&gThunder_.handle,  0.9f);

    firstFrame_ = true;
    return true;
}

void Soundscape::shutdown() {
    if (!engineInit_) return;

    // stop and free loop slots
    for (auto& s : slots_) {
        s.uninitNext();
        s.uninitCurrent();
    }
    uninitGroups_();
    uninitEngine_();
}

bool Soundscape::initEngine_() {
    if (engineInit_) return true;
    // Use default engine configuration for simplicity:
    // miniaudio will create device, resource manager, node graph, etc. for us. :contentReference[oaicite:4]{index=4}
    ma_result r = ma_engine_init(nullptr, &engine_);
    if (r != MA_SUCCESS) {
        std::cerr << "[Soundscape] ma_engine_init failed (" << r << ")\n";
        return false;
    }
    engineInit_ = true;
    return true;
}

void Soundscape::uninitEngine_() {
    if (!engineInit_) return;
    ma_engine_uninit(&engine_);
    engineInit_ = false;
}

void Soundscape::initGroups_() {
    auto mk = [&](Group& g, Group* parent) {
        ma_sound_group* parentPtr = parent ? &parent->handle : nullptr;
        if (ma_sound_group_init(&engine_, 0, parentPtr, &g.handle) == MA_SUCCESS) {
            g.initialized = true;
        }
    };
    mk(gMaster_, nullptr);
    mk(gBed_,     &gMaster_);
    mk(gWind_,    &gMaster_);
    mk(gWater_,   &gMaster_);
    mk(gWildlife_,&gMaster_);
    mk(gRain_,    &gMaster_);
    mk(gEvent_,   &gMaster_);
    mk(gThunder_, &gMaster_);
}

void Soundscape::uninitGroups_() {
    Group* groups[] = { &gThunder_, &gEvent_, &gRain_, &gWildlife_, &gWater_, &gWind_, &gBed_, &gMaster_ };
    for (Group* g : groups) {
        if (g->initialized) {
            ma_sound_group_uninit(&g->handle);
            g->initialized = false;
        }
    }
}

void Soundscape::setMasterVolumeDb(float db) {
    if (!engineInit_) return;
    ma_sound_group_set_volume(&gMaster_.handle, dbToLinear_(db));
}

void Soundscape::muteAll(bool mute)       { ma_sound_group_set_volume(&gMaster_.handle, mute ? 0.0f : 1.0f); }
void Soundscape::muteWildlife(bool mute)  { ma_sound_group_set_volume(&gWildlife_.handle, mute ? 0.0f : 1.0f); }
void Soundscape::muteWeather(bool mute)   { ma_sound_group_set_volume(&gRain_.handle, mute ? 0.0f : 1.0f); }

// --------------------------------------
// Procedural selection
// --------------------------------------
std::string Soundscape::chooseBedLoop_(const WorldState& s) const {
    const std::string root = params_.assetRoot;
    using B = Biome; using D = DayPhase;

    switch (s.biome) {
        case B::Forest:   return pathJoin(root, s.dayPhase == D::Night ? kPaths.bedForestNight : kPaths.bedForestDay);
        case B::Desert:   return pathJoin(root, s.dayPhase == D::Night ? kPaths.bedDesertNight : kPaths.bedDesertDay);
        case B::Plains:   return pathJoin(root, s.dayPhase == D::Night ? kPaths.bedPlainsNight : kPaths.bedPlainsDay);
        case B::Snow:     return pathJoin(root, s.dayPhase == D::Night ? kPaths.bedSnowNight   : kPaths.bedSnowDay);
        case B::Swamp:    return pathJoin(root, kPaths.bedSwampNight);   // swamp shines at night
        case B::Mountain: return pathJoin(root, kPaths.bedMountainDay);
        case B::Ocean:    return pathJoin(root, kPaths.bedOceanDay);
        case B::Caves:    return pathJoin(root, kPaths.bedCavesNight);
        default:          return pathJoin(root, kPaths.bedPlainsDay);
    }
}

std::string Soundscape::chooseWindLoop_(const WorldState& s) const {
    float w = clamp01(s.windIntensity);
    if      (w < 0.33f) return pathJoin(params_.assetRoot, kPaths.windLight);
    else if (w < 0.66f) return pathJoin(params_.assetRoot, kPaths.windMed);
    else                return pathJoin(params_.assetRoot, kPaths.windHeavy);
}

std::string Soundscape::chooseWaterLoop_(const WorldState& s) const {
    // Encourage coast water in Ocean/Mountain, swamp water in Swamp, stream otherwise if humidity is high.
    using B = Biome;
    if (s.biome == B::Ocean)      return pathJoin(params_.assetRoot, kPaths.waterCoast);
    if (s.biome == B::Swamp)      return pathJoin(params_.assetRoot, kPaths.waterSwamp);
    if (s.humidityOrWater > 0.5f) return pathJoin(params_.assetRoot, kPaths.waterStream);
    return std::string(); // no water bed
}

std::string Soundscape::chooseWildlifeLoop_(const WorldState& s) const {
    using D = DayPhase; using B = Biome;
    if (s.dayPhase == D::Day) {
        // Birds in most biomes during day
        // pick one of the two bird beds pseudo-randomly by biome to avoid flip-flopping every frame
        return pathJoin(params_.assetRoot, (int(s.biome) % 2 == 0) ? kPaths.birdsDay1 : kPaths.birdsDay2);
    } else {
        // Night fauna depends on biome
        if (s.biome == B::Swamp) return pathJoin(params_.assetRoot, kPaths.frogs);
        if (s.biome == B::Snow)  return pathJoin(params_.assetRoot, kPaths.wolves);
        return pathJoin(params_.assetRoot, kPaths.crickets);
    }
}

std::string Soundscape::chooseRainLoop_(const WorldState& s) const {
    using W = Weather;
    if (s.weather == W::Rain)      return pathJoin(params_.assetRoot, kPaths.rainLight);
    if (s.weather == W::HeavyRain) return pathJoin(params_.assetRoot, kPaths.rainHeavy);
    if (s.weather == W::Storm)     return pathJoin(params_.assetRoot, kPaths.stormBed);
    return std::string();
}

std::string Soundscape::chooseEventLoop_(const WorldState& s) const {
    // Map "danger" into a tension layer; you can swap this to any tension loop you like.
    // For demo we'll reuse wind heavy as a low, ominous rumble or a custom file if you add it:
    if (s.dangerLevel > 0.2f) {
        return pathJoin(params_.assetRoot, kPaths.windHeavy);
    }
    return std::string();
}

std::string Soundscape::chooseThunderOneShot_(const WorldState& s) const {
    using W = Weather;
    if (s.weather != W::Storm) return std::string();
    // Pick a thunder candidate at random; oneshots are driven from scheduler.
    std::uniform_int_distribution<size_t> dist(0, kPaths.thunder.size() - 1);
    return pathJoin(params_.assetRoot, kPaths.thunder[dist(rng_)]);
}

// --------------------------------------
// Core update
// --------------------------------------
void Soundscape::update(const WorldState& s, float dtSeconds) {
    if (!engineInit_) return;

    // Choose desired loops for each category
    const std::string bed      = chooseBedLoop_(s);
    const std::string wind     = chooseWindLoop_(s);
    const std::string water    = chooseWaterLoop_(s);
    const std::string wildlife = chooseWildlifeLoop_(s);
    const std::string rain     = chooseRainLoop_(s);
    const std::string ev       = chooseEventLoop_(s);

    // Volumes (linear) per category based on world parameters
    // Tuned simply; tweak as you like
    const float bedVol      = 0.50f;
    const float windVol     = lerp(0.10f, 0.55f, clamp01(s.windIntensity));
    const float waterVol    = (water.empty() ? 0.0f : lerp(0.15f, 0.50f, clamp01(s.humidityOrWater)));
    const float wildlifeVol = (s.dayPhase == DayPhase::Day ? 0.4f : 0.35f);
    const float rainVol     = (!rain.empty() ? (s.weather == Weather::Storm ? 0.7f : 0.5f) : 0.0f);
    const float eventVol    = lerp(0.0f, 0.55f, clamp01(s.dangerLevel));

    ensureLoop_(Cat::Bed,      bed,      bedVol,      kDefaultCrossfadeMs);
    ensureLoop_(Cat::Wind,     wind,     windVol,     kDefaultCrossfadeMs);
    ensureLoop_(Cat::Water,    water,    waterVol,    kDefaultCrossfadeMs);
    ensureLoop_(Cat::Wildlife, wildlife, wildlifeVol, kDefaultCrossfadeMs);
    ensureLoop_(Cat::Rain,     rain,     rainVol,     kDefaultCrossfadeMs);
    ensureLoop_(Cat::Event,    ev,       eventVol,    kDefaultCrossfadeMs);

    // Thunder group volume anchored near unity; it's triggered as one-shots
    setGroupVolLinear_(Cat::Thunder, 1.0f);

    // Update crossfades
    updateCrossfades_(dtSeconds * 1000.0f);

    // Update one-shots (birds/frogs/wolves intermittently; thunder during storms)
    updateOneShots_(s, dtSeconds);

    lastState_  = s;
    firstFrame_ = false;
}

void Soundscape::ensureLoop_(Category cat, const std::string& path, float targetVolLinear, int fadeMs) {
    auto& slot = slots_[(size_t)cat];
    if (!slot.group || !slot.group->initialized) return;

    // Adjust group volume always
    setGroupVolLinear_(cat, targetVolLinear);

    // If empty path => fade out current and stop
    if (path.empty()) {
        if (slot.curInit) {
            // fade current to 0
            ma_sound_set_fade_in_milliseconds(&slot.current, -1.0f, 0.0f, fadeMs); // -1 uses current volume. :contentReference[oaicite:5]{index=5}
            slot.crossfadeMsRemaining = (float)fadeMs;
        }
        return;
    }

    // If the same file is already active, nothing to do.
    if (slot.curInit && slot.curPath == path) {
        return;
    }

    // Prepare next
    if (slot.nextInit) {
        // If next is already requested but different, stop and uninit; we'll create new
        if (slot.nextPath != path) {
            slot.uninitNext();
        }
    }

    if (!slot.nextInit) {
        ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH; // small perf boost; we don't pitch ambient
        // Load from file + attach to category group
        ma_result r = ma_sound_init_from_file(&engine_, path.c_str(), flags, &slot.group->handle, nullptr, &slot.next);
        if (r != MA_SUCCESS) {
            // If loading failed, just bail out; not fatal.
            return;
        }
        slot.nextInit = true;
        slot.nextPath = path;

        // Loop & fade in to 1.0 (group controls absolute level)
        ma_sound_set_looping(&slot.next, MA_TRUE);
        ma_sound_set_fade_in_milliseconds(&slot.next, 0.0f, 1.0f, fadeMs); // fade in next
        ma_sound_start(&slot.next);

        // Fade out current
        if (slot.curInit) {
            ma_sound_set_fade_in_milliseconds(&slot.current, -1.0f, 0.0f, fadeMs);
        }
        slot.crossfadeMsRemaining = (float)fadeMs;
    }
}

void Soundscape::updateCrossfades_(float dtMs) {
    for (auto& slot : slots_) {
        if (slot.crossfadeMsRemaining > 0.0f) {
            slot.crossfadeMsRemaining = std::max(0.0f, slot.crossfadeMsRemaining - dtMs);
            if (slot.crossfadeMsRemaining <= 0.0f) {
                // Crossfade finished; stop & swap
                if (slot.curInit) {
                    ma_sound_stop(&slot.current);
                    ma_sound_uninit(&slot.current);
                    slot.curInit = false;
                }
                if (slot.nextInit) {
                    slot.current   = slot.next;
                    slot.curInit   = true;
                    slot.curPath   = slot.nextPath;

                    // Clear "next" without uninitializing now that we've moved ownership
                    slot.nextInit  = false;
                    slot.nextPath.clear();
                }
            }
        }
    }
}

void Soundscape::setGroupVolLinear_(Category cat, float vol) {
    auto& g = *slots_[(size_t)cat].group;
    if (g.initialized) ma_sound_group_set_volume(&g.handle, clamp01(vol));
}

float Soundscape::dbToLinear_(float db) const {
    return ma_volume_db_to_linear(db); // provided by miniaudio. :contentReference[oaicite:6]{index=6}
}

// --------------------------------------
// One-shot scheduling
// --------------------------------------
static float randRange(std::mt19937& rng, float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng);
}

void Soundscape::updateOneShots_(const WorldState& s, float dt) {
    // Helper for driving a schedule
    auto drive = [&](OneShotSchedule& sch, bool shouldBeActive) {
        if (!sch.enabled || !shouldBeActive || sch.candidates.empty() || !sch.group || !sch.group->initialized) {
            sch.timer = std::max(0.0f, sch.timer - dt); // keep counting down
            return;
        }
        if (sch.timer > 0.0f) { sch.timer -= dt; return; }
        // Time to fire one
        std::uniform_int_distribution<size_t> pick(0, sch.candidates.size() - 1);
        const std::string& file = sch.candidates[pick(rng_)];

        // Fire-and-forget inline sound into the correct group. The last param lets us choose group. :contentReference[oaicite:7]{index=7}
        ma_engine_play_sound(&engine_, file.c_str(), &sch.group->handle);

        // Apply ad-hoc gain to the most recently played sound in the group if you want fine control,
        // but for simplicity we rely on the asset's mastering + group volume here.

        // Reset timer
        sch.timer = randRange(rng_, sch.minDelay, sch.maxDelay);
    };

    // Birds during day in most biomes
    bool birdsActive =
        (s.dayPhase == DayPhase::Day) &&
        (s.weather != Weather::HeavyRain) &&
        (s.weather != Weather::Storm);

    // Frogs at night in swamp
    bool frogsActive = (s.biome == Biome::Swamp) && (s.dayPhase == DayPhase::Night);

    // Wolves at night in snowy biomes
    bool wolvesActive = (s.biome == Biome::Snow) && (s.dayPhase == DayPhase::Night);

    // Thunder during storms
    bool thunderActive = (s.weather == Weather::Storm);

    drive(birdsDay_,  birdsActive);
    drive(frogsNight_,frogsActive);
    drive(wolvesNight_, wolvesActive);
    drive(thunder_,   thunderActive);
}

} // namespace audio
