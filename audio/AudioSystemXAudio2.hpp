// audio/AudioSystemXAudio2.hpp
#pragma once
#include "IAudioSystem.hpp"
#include <xaudio2.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <array>
#include <unordered_map>
#include <mutex>

class AudioSystemXAudio2 final
    : public IAudioSystem
    , public IXAudio2EngineCallback
{
public:
    AudioSystemXAudio2();
    ~AudioSystemXAudio2() override;

    bool initialize() override;
    void shutdown() override;
    void update(float dt) override;

    SoundId loadFromFile(const std::wstring& path) override;
    void unload(SoundId snd) override;

    Instance play(SoundId snd, AudioBus bus, const PlayParams& p = {}) override;
    void stop(Instance inst) override;
    void pause(Instance inst) override;
    void resume(Instance inst) override;
    void setInstanceVolume(Instance inst, float volume) override;
    void setInstancePitch(Instance inst, float ratio) override;

    void setBusVolume(AudioBus bus, float linear) override;
    float getBusVolume(AudioBus bus) const override;

    void triggerDuck(AudioBus targetBus, const DuckParams& d) override;

    // IXAudio2EngineCallback
    void OnProcessingPassStart() noexcept override {}
    void OnProcessingPassEnd()   noexcept override {}
    void OnCriticalError(HRESULT hr) noexcept override; // device invalidated

private:
    struct PCMSound {
        std::vector<uint8_t> data;    // interleaved PCM
        WAVEFORMATEXTENSIBLE wfx{};   // format (first member is WAVEFORMATEX)
        uint32_t             frames = 0;
    };

    struct VoiceCallback final : public IXAudio2VoiceCallback {
        AudioSystemXAudio2* sys = nullptr;
        Instance inst = 0;
        // IXAudio2VoiceCallback impl (only what we use)
        void OnBufferEnd(void* pBufferContext) noexcept override;
        void OnVoiceProcessingPassStart(UINT32) noexcept override {}
        void OnVoiceProcessingPassEnd() noexcept override {}
        void OnStreamEnd() noexcept override {}
        void OnBufferStart(void*) noexcept override {}
        void OnLoopEnd(void*) noexcept override {}
        void OnVoiceError(void*, HRESULT) noexcept override {}
    };

    struct Playing {
        IXAudio2SourceVoice* voice = nullptr;
        SoundId sound = 0;
        AudioBus bus = AudioBus::SFX;
        bool looping = false;
        float volume = 1.f;
        float pitch  = 1.f;
        VoiceCallback cb{};
        bool alive = false;
    };

    struct Ducker {
        float base = 1.f;     // user-set bus volume
        float current = 1.f;  // envelope multiplier (ducking)
        float target = 1.f;   // where we’re heading now
        float attack = 0.03f, hold = 0.10f, release = 0.25f;
        float t = 0.f;
        enum class State { Idle, Attack, Hold, Release } st = State::Idle;
        void trigger(float duckLinear, float a, float h, float r) {
            target = duckLinear; attack=a; hold=h; release=r; t=0.f; st=State::Attack;
        }
        void update(float dt) {
            auto lerp = [](float a,float b,float t){return a+(b-a)*t;};
            switch (st) {
              case State::Idle: current = 1.f; break;
              case State::Attack:
                  t += dt; current = lerp(1.f, target, std::min(t/attack, 1.f));
                  if (t >= attack) { st = State::Hold; t=0.f; }
                  break;
              case State::Hold:
                  t += dt; current = target;
                  if (t >= hold) { st = State::Release; t=0.f; }
                  break;
              case State::Release:
                  t += dt; current = lerp(target, 1.f, std::min(t/release,1.f));
                  if (t >= release) { st = State::Idle; current=1.f; }
                  break;
            }
        }
        float finalVolume() const { return std::max(0.f, base * current); }
    };

    // helpers
    static float dbToLinear(float db) { return powf(10.f, db/20.f); }

    bool createEngineAndVoices();
    void destroyEngine();
    Instance allocInstanceId();
    IXAudio2SourceVoice* createSourceVoiceFor(SoundId snd, AudioBus bus, VoiceCallback* cb);
    void applyBusVolumes();

    // state
    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_master = nullptr;
    std::array<IXAudio2SubmixVoice*, (size_t)AudioBus::Count> m_bus{}; // one per bus except Master (nullptr)

    std::unordered_map<SoundId, PCMSound> m_sounds;
    std::unordered_map<Instance, Playing> m_playing;
    Instance m_nextInst = 1;
    SoundId  m_nextSound = 1;

    std::array<Ducker, (size_t)AudioBus::Count> m_duckers;
    std::atomic<bool> m_needReinit{false};
    mutable std::mutex m_mtx;
};
