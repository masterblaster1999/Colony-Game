#pragma once
// xaudio_engine.h
// Lightweight event system on top of XAudio2 with bus routing and ambience control.

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <random>
#include <optional>
#include <utility>
#include <functional>

#define NOMINMAX
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

#pragma comment(lib, "xaudio2.lib")

namespace colony::audio {

// ---------- Utility ----------

struct RangeF {
    float min = 0.0f;
    float max = 0.0f;
};

inline float Clamp(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float SemitonesToRatio(float semis) { return std::pow(2.0f, semis / 12.0f); }

// ---------- Buses ----------

enum class AudioBus : uint8_t {
    Master = 0,
    SFX    = 1,
    Music  = 2,
    Ambience = 3,
    COUNT
};

// ---------- Biome / Climate ----------
// Adjust to match your worldgen enums, or map externally.

enum class Biome : uint8_t {
    Desert, Tundra, Forest, Plains, Wetlands, Ocean, Mountains, Savanna, Unknown
};

enum class Climate : uint8_t {
    Polar, Temperate, Tropical, Arid, Continental, Mediterranean, Unknown
};

// Hash for (Biome, Climate) keys
struct AmbienceKey {
    Biome biome = Biome::Unknown;
    Climate climate = Climate::Unknown;
    bool operator==(const AmbienceKey& o) const noexcept {
        return biome == o.biome && climate == o.climate;
    }
};
struct AmbienceKeyHasher {
    size_t operator()(const AmbienceKey& k) const noexcept {
        return (static_cast<size_t>(k.biome) * 131) ^ static_cast<size_t>(k.climate);
    }
};

// ---------- Clip ----------
// In-memory WAV (PCM or IEEE float). If you need streaming, extend this.

struct WavData {
    // Either WAVEFORMATEX or WAVEFORMATEXTENSIBLE inlined here.
    // We keep the larger one and provide a WAVEFORMATEX* view.
    WAVEFORMATEXTENSIBLE fmtExt{};
    bool isExtensible = false;

    std::vector<uint8_t> samples;    // raw PCM/float interleaved
    uint32_t sampleBytesPerFrame = 0; // block align

    const WAVEFORMATEX* Wfx() const noexcept {
        return isExtensible ? &fmtExt.Format : reinterpret_cast<const WAVEFORMATEX*>(&fmtExt);
    }
};

using ClipPtr = std::shared_ptr<WavData>;

// ---------- Event description & handles ----------

struct AudioEventDesc {
    // One event can randomly pick any of these clips each time it plays.
    std::vector<std::string> clipIds;

    AudioBus bus = AudioBus::SFX;
    bool loop = false;
    uint32_t maxPolyphony = 8;          // concurrent instances allowed
    float baseVolume = 1.0f;            // linear (1.0 = unity)
    RangeF volumeJitter = {0.9f, 1.1f}; // random multiplier [min, max]
    RangeF pitchSemitoneJitter = {-0.25f, 0.25f}; // added semitones per trigger
    float startDelaySec = 0.0f;         // simple start delay (handled as fade from 0)

    // Optional: per-event fade-in/out defaults
    float defaultFadeInSec  = 0.02f;
    float defaultFadeOutSec = 0.05f;
};

struct AudioEventHandle {
    uint32_t id = 0; // 0 = invalid
    bool Valid() const noexcept { return id != 0; }
};

// ---------- Engine ----------

class XAudioEngine {
public:
    XAudioEngine() = default;
    ~XAudioEngine();

    // Lifecycle
    bool Init();   // returns false on failure; safe to call once
    void Shutdown();

    // Per-frame (advance fades, reap finished voices)
    void Update(float dtSeconds);

    // Content (one-time) -------------------------------------------------------
    bool RegisterClip(const std::string& id, const std::wstring& wavPath); // WAV only
    bool RegisterEvent(const std::string& name, const AudioEventDesc& desc);
    void UnregisterClip(const std::string& id);
    void UnregisterEvent(const std::string& name);

    // Playback ----------------------------------------------------------------
    AudioEventHandle Play(const std::string& eventName,
                          float volumeScale = 1.0f,
                          float pitchSemitoneOffset = 0.0f);

    // Stop a specific instance (if still alive)
    void Stop(const AudioEventHandle& handle, float fadeOutSec = 0.05f);

    // Stop all instances of an event
    void StopEvent(const std::string& eventName, float fadeOutSec = 0.1f);

    // Global stop
    void StopAll(float fadeOutSec = 0.1f);

    // Buses -------------------------------------------------------------------
    void SetBusVolume(AudioBus bus, float volume /* linear [0..1..] */);
    float GetBusVolume(AudioBus bus) const;

    void SetMasterVolume(float volume);
    float GetMasterVolume() const;

    // Ambience ----------------------------------------------------------------
    // Map (Biome, Climate) -> event name (must be registered & looping)
    void RegisterAmbience(Biome biome, Climate climate, const std::string& eventName);
    void ClearAmbienceMap();

    // Crossfade to ambience for biome/climate (no-op if already active)
    void SetAmbience(Biome biome, Climate climate, float crossfadeSec = 2.0f);

    // Optional: direct ambience by event
    void SetAmbienceByEvent(const std::string& eventName, float crossfadeSec = 2.0f);

private:
    struct VoiceInstance {
        uint32_t id = 0;
        IXAudio2SourceVoice* voice = nullptr;
        ClipPtr clip;
        std::string eventName;

        // Volume/pitch state
        float baseVolume = 1.0f;
        float volumeScale = 1.0f;  // external multiplier (Play argument)
        float currentVol = 0.0f;   // current applied volume (linear)
        float targetVol = 1.0f;    // target volume for fades
        float fadeTime = 0.0f;
        float fadeElapsed = 0.0f;
        bool  fadeToSilenceThenStop = false;

        bool looping = false;
        AudioBus bus = AudioBus::SFX;

        // Utility for cleanup
        bool stopRequested = false;
    };

    // Internal helpers
    VoiceInstance* PlayInternal(const AudioEventDesc& desc, const std::string& eventName,
                                float volumeScale, float pitchSemitoneOffset);
    void DestroyVoice(VoiceInstance& inst);
    void TickVoice(VoiceInstance& inst, float dt);
    void ReapFinishedVoices();
    IXAudio2SubmixVoice* BusToSubmix(AudioBus bus) const;

    // WAV loading
    bool LoadWav(const std::wstring& path, WavData& outData, std::string* outErr = nullptr) const;

private:
    // XAudio2 core
    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SubmixVoice* m_submix[(int)AudioBus::COUNT] = {nullptr, nullptr, nullptr, nullptr};

    // Registries
    std::unordered_map<std::string, ClipPtr> m_clips;
    std::unordered_map<std::string, AudioEventDesc> m_events;

    // Playing instances
    std::unordered_map<uint32_t, VoiceInstance> m_voicesById;
    std::multimap<std::string, uint32_t> m_eventToVoiceIds; // event name -> IDs
    uint32_t m_nextId = 1;

    // Ambience mapping & active handles
    std::unordered_map<AmbienceKey, std::string, AmbienceKeyHasher> m_ambienceMap;
    std::optional<AudioEventHandle> m_activeAmbience;
    std::optional<AudioEventHandle> m_prevAmbience;  // fading out

    // RNG
    std::mt19937 m_rng{ std::random_device{}() };

    // Master volume cache (asked from engine on demand)
    float m_masterVol = 1.0f;
};

} // namespace colony::audio
