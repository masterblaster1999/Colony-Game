#pragma once
// xaudio_engine.h
// Feature-rich event system on XAudio2 with buses, FX, 3D audio, ducking,
// snapshots, RTPCs, scheduling, filters, sends, panning, and telemetry. Windows-only.

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
#include <map>        // std::multimap
#include <cmath>      // std::pow, std::log10
#include <atomic>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <deque>
#include <chrono>

// --- Windows & XAudio2 -------------------------------------------------------
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmreg.h>     // WAVEFORMATEX / WAVEFORMATEXTENSIBLE
#include <xaudio2.h>
#include <wrl/client.h>
#pragma comment(lib, "xaudio2.lib")

// Optional XAudio2FX (Reverb / VolumeMeter) -----------------------------------
#if defined(__has_include)
  #if __has_include(<xaudio2fx.h>)
    #define CG_AUDIO_HAS_XAUDIO2FX 1
    #include <xaudio2fx.h>
  #else
    #define CG_AUDIO_HAS_XAUDIO2FX 0
  #endif
#else
  #define CG_AUDIO_HAS_XAUDIO2FX 1
  #include <xaudio2fx.h>
#endif

// Optional XAPOFX (EQ/Echo/MasteringLimiter) ----------------------------------
#if defined(__has_include)
  #if __has_include(<xapofx.h>)
    #define CG_AUDIO_HAS_XAPOFX 1
    #include <xapofx.h>
    #pragma comment(lib, "xapofx.lib")
  #else
    #define CG_AUDIO_HAS_XAPOFX 0
  #endif
#else
  #define CG_AUDIO_HAS_XAPOFX 0
#endif

// Optional X3DAudio (spatialization helpers) ----------------------------------
#if defined(__has_include)
  #if __has_include(<x3daudio.h>)
    #define CG_AUDIO_HAS_X3DAUDIO 1
    #include <x3daudio.h>
  #else
    #define CG_AUDIO_HAS_X3DAUDIO 0
  #endif
#else
  #define CG_AUDIO_HAS_X3DAUDIO 1
  #include <x3daudio.h>
#endif

namespace colony::audio {

// ---------- Utility ----------
struct RangeF { float min = 0.0f; float max = 0.0f; };

inline float Clamp(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float SemitonesToRatio(float semis) { return std::pow(2.0f, semis / 12.0f); }
inline float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float LinToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-6f)); }

// Frequency ratio limits from XAUDIO2 (1/1024 .. 1024) for SetFrequencyRatio.  :contentReference[oaicite:8]{index=8}
inline constexpr float kMinFreqRatio = 1.0f / 1024.0f;
inline constexpr float kMaxFreqRatio = 1024.0f;

// ---------- Buses ----------
enum class AudioBus : uint8_t { Master = 0, SFX = 1, Music = 2, Ambience = 3, COUNT };
static constexpr int kBusCount = static_cast<int>(AudioBus::COUNT);

// ---------- Biome / Climate (for ambience) ----------
enum class Biome   : uint8_t { Desert, Tundra, Forest, Plains, Wetlands, Ocean, Mountains, Savanna, Unknown };
enum class Climate : uint8_t { Polar, Temperate, Tropical, Arid, Continental, Mediterranean, Unknown };

struct AmbienceKey {
    Biome biome = Biome::Unknown;
    Climate climate = Climate::Unknown;
    bool operator==(const AmbienceKey& o) const noexcept { return biome == o.biome && climate == o.climate; }
};
struct AmbienceKeyHasher {
    size_t operator()(const AmbienceKey& k) const noexcept {
        return (static_cast<size_t>(k.biome) * 131u) ^ static_cast<size_t>(k.climate);
    }
};

// ---------- Clip ----------
struct WavData {
    // WAVEFORMATEXTENSIBLE embeds a WAVEFORMATEX header in 'Format'.
    WAVEFORMATEXTENSIBLE fmtExt{};
    bool isExtensible = false;

    std::vector<uint8_t> samples;      // raw PCM/float interleaved
    uint32_t sampleBytesPerFrame = 0;  // block align

    const WAVEFORMATEX* Wfx() const noexcept { return &fmtExt.Format; }
};
using ClipPtr = std::shared_ptr<WavData>;

// ---------- Event description & handles ----------

enum class VoiceStealPolicy : uint8_t {
    None,       // refuse if over polyphony
    Oldest,     // stop the oldest instance
    Newest,     // stop the most recent instance
    Quietest    // stop instance with lowest current volume
};

enum class FadeCurve : uint8_t { Linear, EaseIn, EaseOut, Exponential, Sine };

struct FadeParams {
    float inSec  = 0.02f;
    float outSec = 0.05f;
    FadeCurve curve = FadeCurve::Linear;
};

struct ClipChoice {
    std::string clipId;
    float weight = 1.0f;                    // weighted random selection
    RangeF volumeJitter {1.0f, 1.0f};       // extra per-choice gain
    RangeF pitchSemitoneJitter {0.0f, 0.0f};
    float startOffsetSec = 0.0f;            // optional start offset
    float trimEndSec = -1.0f;               // negative = no trim
};

enum class DistanceModel : uint8_t { Inverse, Linear, Exponential };

struct AudioEventDesc {
    // Backwards-compatible: if 'choices' is empty, use 'clipIds'.
    std::vector<std::string> clipIds;
    std::vector<ClipChoice>  choices;

    AudioBus bus = AudioBus::SFX;
    bool loop = false;
    uint32_t maxPolyphony = 8;          // concurrent instances allowed
    VoiceStealPolicy steal = VoiceStealPolicy::Oldest;
    int priority = 0;                   // higher = more important

    float baseVolume = 1.0f;            // linear (1.0 = unity)
    RangeF volumeJitter = {0.9f, 1.1f};
    RangeF pitchSemitoneJitter = {-0.25f, 0.25f};
    float startDelaySec = 0.0f;

    FadeParams fades{ 0.02f, 0.05f, FadeCurve::Linear };

    // 3D defaults (used by Play3D; ignored by Play)
    DistanceModel distanceModel = DistanceModel::Inverse;
    float minDistance = 1.0f;
    float maxDistance = 50.0f;          // used by rolloff in 3D
    float dopplerScalar = 1.0f;         // doppler intensity

    // Virtualization: skip DSP if est. loudness below threshold (saves CPU)
    float virtualizeBelowDb = -72.0f;
};

struct AudioEventHandle {
    uint32_t id = 0; // 0 = invalid
    bool Valid() const noexcept { return id != 0; }
};

// ---------- 3D types (X3DAudio) ----------
struct Vec3 { float x=0, y=0, z=0; };
struct Orientation { Vec3 forward{0,0,1}; Vec3 up{0,1,0}; };

struct Listener3D {
    Vec3 position{};
    Vec3 velocity{};
    Orientation orientation{};
    float dopplerScalar = 1.0f; // overall doppler sensitivity (multiplies per-event)
};

struct Emitter3D {
    Vec3 position{};
    Vec3 velocity{};
    Orientation orientation{};
    float innerRadius = 0.0f;
    float innerRadiusAngle = 0.0f; // radians
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    float dopplerScalar = 1.0f;
    float occlusion = 0.0f;   // 0..1 maps to LPF + gain; see SetOcclusionMapping
    float obstruction = 0.0f; // 0..1 similar to occlusion but mild; summed/clamped
};

// ---------- Engine configuration ----------
struct InitParams {
    bool enable3D = true;          // initialize X3DAudio
    float speedOfSound = 343.0f;   // world units / sec used by doppler calculations
    bool enableLimiterOnMaster = true; // XAPOFX MasteringLimiter if available
};

// Engine performance snapshot (subset of XAUDIO2_PERFORMANCE_DATA). :contentReference[oaicite:9]{index=9}
struct PerformanceData {
    uint32_t activeSourceVoiceCount = 0;
    uint32_t totalVoices = 0;
    uint64_t audioCyclesSinceLastQuery = 0;
    uint64_t totalCyclesSinceLastQuery = 0;
    uint32_t memoryUsageBytes = 0;
    uint32_t currentLatencySamples = 0;
};

// Callback when a playing instance ends (after OnStreamEnd). :contentReference[oaicite:10]{index=10}
using OnEventEnd = std::function<void(const AudioEventHandle&, const std::string& eventName)>;

// ---------- Engine ----------
class XAudioEngine {
public:
    XAudioEngine() = default;
    ~XAudioEngine();

    // Lifecycle
    bool Init();                         // default params
    bool Init(const InitParams& p);      // extended init
    void Shutdown();

    // Transport / scheduling ---------------------------------------------------
    void Update(float dtSeconds);
    void Pause(bool pause);
    bool IsPaused() const;
    // Schedule an event to play in the future (relative to now).
    // Returns a schedule id you can cancel before it starts.
    uint64_t SchedulePlay(const std::string& eventName,
                          double delaySec,
                          float volumeScale = 1.0f,
                          float pitchSemitoneOffset = 0.0f);
    void CancelScheduled(uint64_t scheduleId);

    // Content (one-time) -------------------------------------------------------
    bool RegisterClip(const std::string& id, const std::wstring& wavPath); // WAV only
    void UnregisterClip(const std::string& id);

    bool RegisterEvent(const std::string& name, const AudioEventDesc& desc);
    void UnregisterEvent(const std::string& name);
    void PreloadEvent(const std::string& name);

    // Playback (2D) ------------------------------------------------------------
    AudioEventHandle Play(const std::string& eventName,
                          float volumeScale = 1.0f,
                          float pitchSemitoneOffset = 0.0f);

    // 3D playback (if enabled) -------------------------------------------------
    AudioEventHandle Play3D(const std::string& eventName,
                            const Emitter3D& emitter,
                            float volumeScale = 1.0f,
                            float pitchSemitoneOffset = 0.0f);

    void SetListener(const Listener3D& l);
    Listener3D GetListener() const;

    // Instance controls --------------------------------------------------------
    void SetInstanceVolume(const AudioEventHandle& h, float linearVol);
    void SetInstancePitchSemitones(const AudioEventHandle& h, float semitones);
    void SetInstance3D(const AudioEventHandle& h, const Emitter3D& emitter);

    // 2D pan in [-1..+1] (stereo/greater mixes via SetOutputMatrix). :contentReference[oaicite:11]{index=11}
    void SetInstancePan(const AudioEventHandle& h, float pan);

    // Adjust send level from instance to a target bus (for reverb/aux). :contentReference[oaicite:12]{index=12}
    void SetInstanceSendLevel(const AudioEventHandle& h, AudioBus dstBus, float linear);

    // Filters (per-voice) ------------------------------------------------------
    // XAudio2 supports built-in voice filters (LPF/HPF/BPF).  :contentReference[oaicite:13]{index=13}
    void SetInstanceLowPass(const AudioEventHandle& h, float cutoffHz /*>0 or 0=off*/);
    void SetInstanceHighPass(const AudioEventHandle& h, float cutoffHz /*>0 or 0=off*/);
    void SetInstanceBandPass(const AudioEventHandle& h, float centerHz, float oneOverQ);

    // Stop a specific instance (if still alive)
    void Stop(const AudioEventHandle& handle, float fadeOutSec = 0.05f);

    // Stop all instances of an event
    void StopEvent(const std::string& eventName, float fadeOutSec = 0.1f);

    // Global stop
    void StopAll(float fadeOutSec = 0.1f);

    // Buses --------------------------------------------------------------------
    void SetBusVolume(AudioBus bus, float volume /* linear [0..1..] */);
    float GetBusVolume(AudioBus bus) const;

    void SetMasterVolume(float volume);
    float GetMasterVolume() const;

    void MuteBus(AudioBus bus, bool mute);
    bool IsBusMuted(AudioBus bus) const;
    void SoloBus(AudioBus bus, bool solo);
    bool IsBusSolo(AudioBus bus) const;

    // Bus FX (reverb/EQ/meter/limiter) ----------------------------------------
#if CG_AUDIO_HAS_XAUDIO2FX
    void EnableBusReverb(AudioBus bus, bool enable);
    void SetBusReverbParams(AudioBus bus, const XAUDIO2FX_REVERB_PARAMETERS& p); // scale time params for non-48kHz voices. :contentReference[oaicite:14]{index=14}
    bool GetBusReverbParams(AudioBus bus, XAUDIO2FX_REVERB_PARAMETERS& out) const;

    void EnableBusMeter(AudioBus bus, bool enable);
    bool GetBusMeterLevels(AudioBus bus, std::vector<float>& channelPeaks /*filled per bus*/) const;
#endif
#if CG_AUDIO_HAS_XAPOFX
    void EnableBusEQ(AudioBus bus, bool enable);     // FXEQ. :contentReference[oaicite:15]{index=15}
    void SetBusEQParams(AudioBus bus, const FXEQ_PARAMETERS& p);

    void EnableBusEcho(AudioBus bus, bool enable);   // FXECHO. :contentReference[oaicite:16]{index=16}
    void SetBusEchoParams(AudioBus bus, const FXECHO_PARAMETERS& p);

    // Optional safety limiter on Master (if supported).
    void EnableMasterLimiter(bool enable);           // FXMasteringLimiter. :contentReference[oaicite:17]{index=17}
#endif

    // Ducking (side-chain) ----------------------------------------------------
    // Reduce 'ducked' bus when 'ducker' bus has activity.
    void AddDuckingRule(AudioBus ducked, AudioBus ducker, float attenuationDb /*-dB*/,
                        float attackSec = 0.08f, float releaseSec = 0.25f);
    void ClearDuckingRules();

    // Ambience ----------------------------------------------------------------
    // Map (Biome, Climate) -> event name (must be registered & looping)
    void RegisterAmbience(Biome biome, Climate climate, const std::string& eventName);
    void ClearAmbienceMap();

    // Crossfade to ambience for biome/climate (no-op if already active)
    void SetAmbience(Biome biome, Climate climate, float crossfadeSec = 2.0f);

    // Optional: direct ambience by event
    void SetAmbienceByEvent(const std::string& eventName, float crossfadeSec = 2.0f);

    // Snapshots ---------------------------------------------------------------
    struct Snapshot {
        float busVolumes[kBusCount]{};
        bool  busMutes[kBusCount]{};
        bool  busSolos[kBusCount]{};
        // Implementation may store FX parameters as needed.
    };
    Snapshot CaptureSnapshot() const;
    void ApplySnapshot(const Snapshot& s, float fadeSec = 0.2f);

    // RTPCs (Runtime Parameter Controls) -------------------------------------
    void SetRTPC(const std::string& name, std::function<void(float)> fn);
    void RemoveRTPC(const std::string& name);
    void UpdateRTPC(const std::string& name, float value);
    void ClearRTPCs();

    // Occlusion/obstruction mapping (applied by Update/SetInstance3D) --------
    // Configure how occlusion values (0..1) map to LPF cutoff and volume.
    void SetOcclusionMapping(float minCutoffHz, float maxCutoffHz,
                             float minGainLinear /*<=1*/, float maxGainLinear /*<=1*/);

    // Queries & telemetry -----------------------------------------------------
    uint32_t GetActiveInstanceCount(const std::string& eventName) const;
    uint32_t GetTotalActiveInstances() const;
    PerformanceData GetPerformanceData() const; // wraps IXAudio2::GetPerformanceData. :contentReference[oaicite:18]{index=18}

    // Callbacks ---------------------------------------------------------------
    void SetOnEventEnd(OnEventEnd cb);

private:
    // Voice state
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

        // 3D (optional)
        bool is3D = false;
        Emitter3D emitter{};
        DistanceModel distanceModel = DistanceModel::Inverse;

        // 2D pan [-1..+1] for stereo/greater (implemented via SetOutputMatrix)
        float pan = 0.0f;

        // Utility for cleanup
        bool stopRequested = false;
    };

    // Scheduling
    struct Scheduled {
        uint64_t id = 0;
        double   triggerTime = 0.0; // engine-relative seconds
        std::string eventName;
        float volumeScale = 1.0f;
        float pitchSemitones = 0.0f;
        std::optional<Emitter3D> emitter3D;
    };

    // Internal helpers
    VoiceInstance* PlayInternal(const AudioEventDesc& desc, const std::string& eventName,
                                float volumeScale, float pitchSemitoneOffset, const Emitter3D* emitterOpt);
    void DestroyVoice(VoiceInstance& inst);
    void TickVoice(VoiceInstance& inst, float dt);
    void ApplyFade(VoiceInstance& inst, float dt);
    void ApplyPan(VoiceInstance& inst); // SetOutputMatrix to bus. :contentReference[oaicite:19]{index=19}
    void ReapFinishedVoices();
    IXAudio2SubmixVoice* BusToSubmix(AudioBus bus) const;

    // 3D helpers (X3DAudio)
    void Ensure3DInitialized();
    void Apply3DToVoice(VoiceInstance& inst);

    // WAV loading
    bool LoadWav(const std::wstring& path, WavData& outData, std::string* outErr = nullptr) const;

    // Polyphony / stealing
    bool EnforcePolyphony(const std::string& eventName, const AudioEventDesc& desc, uint32_t& outStolenId);

    // Ducking
    void UpdateDucking(float dt);

    // Scheduling
    void UpdateScheduling(double dt);

    // Utility (filters)
    void SetVoiceFilter(IXAudio2SourceVoice* v, XAUDIO2_FILTER_TYPE type, float cutoffHz, float oneOverQ = 1.0f);
    // For occlusion/obstruction
    void ApplyOcclusion(VoiceInstance& inst);

private:
    // XAudio2 core
    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SubmixVoice* m_submix[kBusCount]{}; // zero-initialized

#if CG_AUDIO_HAS_XAUDIO2FX
    struct ReverbSlot { bool enabled=false; IUnknown* fx=nullptr; };
    struct MeterSlot  { bool enabled=false; IUnknown* fx=nullptr; };
    ReverbSlot m_reverb[kBusCount]{};
    MeterSlot  m_meter[kBusCount]{};
#endif
#if CG_AUDIO_HAS_XAPOFX
    struct EQSlot     { bool enabled=false; IUnknown* fx=nullptr; };
    struct EchoSlot   { bool enabled=false; IUnknown* fx=nullptr; };
    EQSlot   m_eq[kBusCount]{};
    EchoSlot m_echo[kBusCount]{};
    bool     m_masterLimiterEnabled = false;
    IUnknown* m_masterLimiter = nullptr;
#endif

#if CG_AUDIO_HAS_X3DAUDIO
    X3DAUDIO_HANDLE m_x3dInstance{};
    Listener3D m_listener{};
    uint32_t m_masterChannelMask = 0; // from mastering voice (GetChannelMask). :contentReference[oaicite:20]{index=20}
    float m_speedOfSound = 343.0f;
#endif

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

    // Ducking rules: ducked <- ducker
    struct DuckRule {
        AudioBus ducked;
        AudioBus ducker;
        float attenDb;
        float attackSec;
        float releaseSec;
        float env = 0.0f; // envelope follower state
    };
    std::vector<DuckRule> m_duckRules;

    // RTPCs
    std::unordered_map<std::string, std::function<void(float)>> m_rtpcs;

    // RNG
    std::mt19937 m_rng{ std::random_device{}() };

    // Mixer cache
    float m_masterVol = 1.0f;
    float m_busVol[kBusCount]{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool  m_busMute[kBusCount]{ false, false, false, false };
    bool  m_busSolo[kBusCount]{ false, false, false, false };

    // Occlusion mapping
    float m_occMinCutHz = 800.0f, m_occMaxCutHz = 20000.0f;
    float m_occMinGain  = 0.25f,  m_occMaxGain  = 1.0f;

    // Scheduling
    std::deque<Scheduled> m_schedule;
    uint64_t m_nextScheduleId = 1;
    double m_timeSec = 0.0;
    bool   m_paused = false;

    // Thread-safety (public API is game-thread oriented; internals guard mutable maps)
    mutable std::shared_mutex m_stateMutex;

    // Callbacks
    OnEventEnd m_onEventEnd;
};

} // namespace colony::audio
