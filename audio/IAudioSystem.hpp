// audio/IAudioSystem.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class AudioBus : uint8_t { Master=0, Music, SFX, UI, Ambient, Voice, Count };

struct DuckParams {
    float duckDb      = -12.f; // amount to reduce target bus in dB (negative)
    float attackSec   = 0.03f;
    float holdSec     = 0.10f;
    float releaseSec  = 0.25f;
};

struct PlayParams {
    float volume = 1.0f;   // linear (0.. >1)
    float pitch  = 1.0f;   // 1.0 = normal, uses SetFrequencyRatio
    bool  loop   = false;
    bool  duckMusicWhilePlaying = false; // convenience
};

using SoundId   = uint32_t;    // loaded sound asset
using Instance  = uint32_t;    // playing instance handle

class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void update(float dt) = 0; // run envelopes (ducking), flush disposals

    // asset lifecycle (expects PCM WAV/OGG/MP3 decoded to PCM by decoders)
    virtual SoundId loadFromFile(const std::wstring& path) = 0;
    virtual void    unload(SoundId snd) = 0;

    // playback
    virtual Instance play(SoundId snd, AudioBus bus, const PlayParams& p = {}) = 0;
    virtual void     stop(Instance inst) = 0;
    virtual void     pause(Instance inst) = 0;
    virtual void     resume(Instance inst) = 0;
    virtual void     setInstanceVolume(Instance inst, float volume) = 0;
    virtual void     setInstancePitch(Instance inst, float ratio) = 0; // SetFrequencyRatio

    // buses
    virtual void setBusVolume(AudioBus bus, float linear) = 0; // immediate set
    virtual float getBusVolume(AudioBus bus) const = 0;

    // ducking
    virtual void triggerDuck(AudioBus targetBus, const DuckParams& d) = 0;
};
