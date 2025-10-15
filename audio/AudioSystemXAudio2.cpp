// audio/AudioSystemXAudio2.cpp
#include "AudioSystemXAudio2.hpp"
#include "AudioDecoders.hpp"
#include <cassert>
#include <cmath>

using Microsoft::WRL::ComPtr;

AudioSystemXAudio2::AudioSystemXAudio2() {}
AudioSystemXAudio2::~AudioSystemXAudio2() { shutdown(); }

bool AudioSystemXAudio2::initialize() {
#ifdef _DEBUG
    UINT32 flags = XAUDIO2_DEBUG_ENGINE;
#else
    UINT32 flags = 0;
#endif
    if (FAILED(XAudio2Create(m_xaudio.GetAddressOf(), flags))) return false;
    m_xaudio->RegisterForCallbacks(this);

    if (FAILED(m_xaudio->CreateMasteringVoice(&m_master))) return false; // default device
    // Create submix buses (route to mastering voice by default)
    for (size_t i=0;i<(size_t)AudioBus::Count;i++) {
        if (i==(size_t)AudioBus::Master) { m_bus[i]=nullptr; m_duckers[i].base = 1.f; continue; }
        // 2 channels, 48k, processing stage 0, default sends (mastering)
        if (FAILED(m_xaudio->CreateSubmixVoice(&m_bus[i], 2, 48000, 0, 0, nullptr, nullptr)))
            return false;
        m_duckers[i].base = 1.f;
    }
    return true;
}

void AudioSystemXAudio2::shutdown() {
    std::scoped_lock lk(m_mtx);
    if (!m_xaudio) return;
    m_xaudio->StopEngine(); // stop processing thread before destroying; avoids race/crash. :contentReference[oaicite:8]{index=8}
    for (auto& kv : m_playing) { if (kv.second.voice) { kv.second.voice->DestroyVoice(); } }
    m_playing.clear();
    for (auto* v : m_bus) if (v) v->DestroyVoice();
    m_bus.fill(nullptr);
    if (m_master) { m_master->DestroyVoice(); m_master=nullptr; }
    m_xaudio->UnregisterForCallbacks(this);
    m_xaudio.Reset();
}

void AudioSystemXAudio2::OnCriticalError(HRESULT) noexcept {
    // Device invalidated (e.g., unplugged). Mark and rebuild next update(). :contentReference[oaicite:9]{index=9}
    m_needReinit.store(true, std::memory_order_relaxed);
}

void AudioSystemXAudio2::update(float dt) {
    if (m_needReinit.exchange(false)) {
        // Tear-down & rebuild graph
        shutdown();
        initialize();
        // (Optional) re-create looping voices, re-route, etc.
    }
    // Update ducking envelopes and apply volumes
    for (auto& d : m_duckers) d.update(dt);
    applyBusVolumes();
}

void AudioSystemXAudio2::applyBusVolumes() {
    for (size_t i=0;i<(size_t)AudioBus::Count;i++) {
        if (!m_bus[i]) continue;
        m_bus[i]->SetVolume(m_duckers[i].finalVolume()); // per-bus volume. :contentReference[oaicite:10]{index=10}
    }
}

SoundId AudioSystemXAudio2::loadFromFile(const std::wstring& path) {
    PCMSound snd{};
    if (!DecodeFileToPCM(path, snd.wfx, snd.data, snd.frames)) { // you implement in AudioDecoders.*
        return 0;
    }
    const SoundId id = m_nextSound++;
    m_sounds.emplace(id, std::move(snd));
    return id;
}

void AudioSystemXAudio2::unload(SoundId snd) {
    std::scoped_lock lk(m_mtx);
    // stop any instances using it
    for (auto it = m_playing.begin(); it != m_playing.end(); ) {
        if (it->second.sound == snd) {
            it->second.voice->DestroyVoice();
            it = m_playing.erase(it);
        } else ++it;
    }
    m_sounds.erase(snd);
}

Instance AudioSystemXAudio2::allocInstanceId() { return m_nextInst++; }

IXAudio2SourceVoice* AudioSystemXAudio2::createSourceVoiceFor(SoundId snd, AudioBus bus, VoiceCallback* cb) {
    auto it = m_sounds.find(snd);
    assert(it != m_sounds.end());
    auto& pcm = it->second;
    // route this source to the chosen submix (bus)
    XAUDIO2_SEND_DESCRIPTOR send{};
    send.Flags = 0;
    send.pOutputVoice = m_bus[(size_t)bus];

    XAUDIO2_VOICE_SENDS sends{};
    sends.SendCount = 1;
    sends.pSends = &send;

    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(m_xaudio->CreateSourceVoice(&v, (WAVEFORMATEX*)&pcm.wfx, 0,
                                           XAUDIO2_DEFAULT_FREQ_RATIO, cb, &sends, nullptr))) {
        return nullptr;
    }
    return v;
}

Instance AudioSystemXAudio2::play(SoundId snd, AudioBus bus, const PlayParams& p) {
    std::scoped_lock lk(m_mtx);
    auto it = m_sounds.find(snd);
    if (it == m_sounds.end()) return 0;

    Instance inst = allocInstanceId();

    Playing pl{};
    pl.sound = snd;
    pl.bus   = bus;
    pl.looping = p.loop;
    pl.volume = p.volume;
    pl.pitch  = p.pitch;
    pl.cb.sys = this;
    pl.cb.inst = inst;

    pl.voice = createSourceVoiceFor(snd, bus, &pl.cb);
    if (!pl.voice) return 0;

    // buffer
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = (UINT32)it->second.data.size();
    buf.pAudioData = it->second.data.data();
    if (p.loop) { buf.LoopCount = XAUDIO2_LOOP_INFINITE; } // loop forever. :contentReference[oaicite:11]{index=11}

    // submit & start
    if (FAILED(pl.voice->SubmitSourceBuffer(&buf))) { pl.voice->DestroyVoice(); return 0; } // :contentReference[oaicite:12]{index=12}
    pl.voice->SetVolume(std::max(0.f, p.volume)); // submix has bus gain; this is per-instance. :contentReference[oaicite:13]{index=13}
    pl.voice->SetFrequencyRatio(std::max(1.f/1024.f, std::min(1024.f, p.pitch))); // pitch. :contentReference[oaicite:14]{index=14}
    if (FAILED(pl.voice->Start())) { pl.voice->DestroyVoice(); return 0; }

    if (p.duckMusicWhilePlaying && bus != AudioBus::Music) {
        DuckParams d{}; // defaults (-12 dB, 30 ms attack, etc.)
        triggerDuck(AudioBus::Music, d);
    }

    pl.alive = true;
    m_playing.emplace(inst, pl);
    return inst;
}

void AudioSystemXAudio2::stop(Instance inst) {
    std::scoped_lock lk(m_mtx);
    auto it = m_playing.find(inst);
    if (it==m_playing.end()) return;
    it->second.voice->Stop();
    it->second.voice->FlushSourceBuffers();
    it->second.voice->DestroyVoice();
    m_playing.erase(it);
}

void AudioSystemXAudio2::pause(Instance inst) {
    std::scoped_lock lk(m_mtx);
    auto it=m_playing.find(inst); if (it==m_playing.end()) return;
    it->second.voice->Stop();
}
void AudioSystemXAudio2::resume(Instance inst) {
    std::scoped_lock lk(m_mtx);
    auto it=m_playing.find(inst); if (it==m_playing.end()) return;
    it->second.voice->Start();
}
void AudioSystemXAudio2::setInstanceVolume(Instance inst, float v) {
    std::scoped_lock lk(m_mtx);
    auto it=m_playing.find(inst); if (it==m_playing.end()) return;
    it->second.voice->SetVolume(std::max(0.f, v));
}
void AudioSystemXAudio2::setInstancePitch(Instance inst, float r) {
    std::scoped_lock lk(m_mtx);
    auto it=m_playing.find(inst); if (it==m_playing.end()) return;
    it->second.voice->SetFrequencyRatio(std::max(1.f/1024.f,std::min(1024.f,r))); // :contentReference[oaicite:15]{index=15}
}

void AudioSystemXAudio2::setBusVolume(AudioBus bus, float linear) {
    m_duckers[(size_t)bus].base = std::max(0.f, linear);
}
float AudioSystemXAudio2::getBusVolume(AudioBus bus) const {
    return m_duckers[(size_t)bus].base;
}

void AudioSystemXAudio2::triggerDuck(AudioBus targetBus, const DuckParams& d) {
    const float duckLinear = dbToLinear(d.duckDb); // e.g., -12 dB ≈ 0.251
    m_duckers[(size_t)targetBus].trigger(duckLinear, d.attackSec, d.holdSec, d.releaseSec);
}

// Voice callback returns instance to “free” when a one‑shot finishes.
void AudioSystemXAudio2::VoiceCallback::OnBufferEnd(void*) noexcept {
    if (!sys) return;
    std::scoped_lock lk(sys->m_mtx);
    auto it = sys->m_playing.find(inst);
    if (it!=sys->m_playing.end() && !it->second.looping) {
        if (it->second.voice) it->second.voice->DestroyVoice();
        sys->m_playing.erase(it);
    }
}
