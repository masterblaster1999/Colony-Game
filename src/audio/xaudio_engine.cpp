// xaudio_engine.cpp
//
// Patch notes:
// - Ensure math constants are available by defining _USE_MATH_DEFINES before <cmath>
//   (adds M_PI, etc.). Also include a fallback #ifndef M_PI guard.
// - Ensure XAudio2 helper inline functions (XAudio2CutoffFrequencyToOnePoleCoefficient,
//   XAudio2CutoffFrequencyToRadians, etc.) are declared by defining XAUDIO2_HELPER_FUNCTIONS
//   before including <xaudio2.h>. This is required on modern Windows SDKs.

// Guard _USE_MATH_DEFINES so we don't trigger C4005 if it's also defined
// on the MSVC command line (/D_USE_MATH_DEFINES).
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#ifndef XAUDIO2_HELPER_FUNCTIONS
#define XAUDIO2_HELPER_FUNCTIONS 1
#endif

#include <xaudio2.h>  // must come after XAUDIO2_HELPER_FUNCTIONS

#include "xaudio_engine.h"

#include <fstream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <limits>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Microsoft::WRL::ComPtr;

namespace colony::audio {

// ============================ Local helpers ============================

static inline float RandRange(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

static inline uint32_t MakeTag(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

static inline float Length(const Vec3& v) {
    return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

static inline Vec3 Sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline float SafeDiv(float a, float b, float def = 0.0f) {
    return (std::abs(b) < 1e-6f) ? def : (a / b);
}

static inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Constant-power stereo pan helper (-1..+1)
static void BuildStereoPanMatrix(float pan /*-1..+1*/, UINT srcCh, UINT dstCh, std::vector<float>& out) {
    out.assign(srcCh * dstCh, 0.0f);
    if (dstCh < 2) return; // only meaningful for stereo-or-more outputs
    float t = Clamp01(0.5f * (pan + 1.0f));         // map to [0..1]
    float l = std::cos(0.5f * float(M_PI) * t);     // constant power
    float r = std::sin(0.5f * float(M_PI) * t);

    // If mono source, send to L/R; if stereo source, scale each accordingly.
    if (srcCh == 1) {
        out[0 * dstCh + 0] = l;
        out[0 * dstCh + 1] = r;
    } else {
        // Simplistic: scale first two channels
        out[0 * dstCh + 0] = l; // left->left
        out[1 * dstCh + 1] = r; // right->right
    }
}

// ============================ XAudioEngine ============================

XAudioEngine::~XAudioEngine() {
    Shutdown();
}

bool XAudioEngine::Init() {
    InitParams p{};
    return Init(p);
}

bool XAudioEngine::Init(const InitParams& p) {
    // Create engine
#if defined(_DEBUG) && (_WIN32_WINNT < 0x0602)
    UINT32 flags = XAUDIO2_DEBUG_ENGINE;
#else
    UINT32 flags = 0;
#endif

    HRESULT hr = XAudio2Create(m_xaudio.GetAddressOf(), flags, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !m_xaudio) return false; // XAudio2Create. :contentReference[oaicite:5]{index=5}

    hr = m_xaudio->CreateMasteringVoice(&m_master);
    if (FAILED(hr) || !m_master) return false;

    // Submix buses (SFX, Music, Ambience → Master)
    XAUDIO2_VOICE_DETAILS masterDetails{};
    m_master->GetVoiceDetails(&masterDetails);
    const UINT32 channels   = masterDetails.InputChannels;
    const UINT32 sampleRate = masterDetails.InputSampleRate;

    auto mkSubmix = [&](AudioBus bus) -> bool {
        IXAudio2SubmixVoice* v = nullptr;
        HRESULT r = m_xaudio->CreateSubmixVoice(&v, channels, sampleRate, 0, 0, nullptr, nullptr);
        if (FAILED(r) || !v) return false;
        m_submix[(int)bus] = v;
        return true;
    };

    if (!mkSubmix(AudioBus::SFX) || !mkSubmix(AudioBus::Music) || !mkSubmix(AudioBus::Ambience)) {
        Shutdown();
        return false;
    }

    // Initialize volumes/mutes/solos
    m_masterVol = 1.0f;
    if (m_master) m_master->SetVolume(m_masterVol);
    for (int i = 0; i < kBusCount; ++i) {
        if (m_submix[i]) m_submix[i]->SetVolume(1.0f);
        m_busVol[i]  = 1.0f;
        m_busMute[i] = false;
        m_busSolo[i] = false;
    }

#if CG_AUDIO_HAS_X3DAUDIO
    // Initialize X3DAudio using mastering voice channel mask, set speed of sound. :contentReference[oaicite:6]{index=6}
    if (p.enable3D) {
        DWORD chmask = 0;
        if (SUCCEEDED(m_master->GetChannelMask(&chmask))) { // GetChannelMask. :contentReference[oaicite:7]{index=7}
            m_masterChannelMask = chmask;
            X3DAudioInitialize(m_masterChannelMask, p.speedOfSound, m_x3dInstance); // X3DAudioInitialize. :contentReference[oaicite:8]{index=8}
            m_speedOfSound = p.speedOfSound;
        }
    }
#endif

#if CG_AUDIO_HAS_XAPOFX
    // Optional mastering limiter on Master
    if (p.enableLimiterOnMaster) {
        if (!m_masterLimiter) {
            IUnknown* fx = nullptr;
            if (SUCCEEDED(CreateFX(__uuidof(FXMasteringLimiter), &fx))) { // CreateFX. :contentReference[oaicite:9]{index=9}
                XAUDIO2_EFFECT_DESCRIPTOR d{}; d.InitialState = TRUE; d.OutputChannels = channels; d.pEffect = fx;
                XAUDIO2_EFFECT_CHAIN chain{1, &d};
                m_master->SetEffectChain(&chain);
                m_masterLimiter = fx;
                m_masterLimiterEnabled = true;
            }
        }
    }
#endif

    m_timeSec = 0.0;
    m_paused  = false;
    return true;
}

void XAudioEngine::Shutdown() {
    // Stop/destroy source voices first
    for (auto& kv : m_voicesById) {
        auto& inst = kv.second;
        if (inst.voice) {
            inst.voice->Stop(0);
            inst.voice->FlushSourceBuffers();
            inst.voice->DestroyVoice();
            inst.voice = nullptr;
        }
    }
    m_voicesById.clear();
    m_eventToVoiceIds.clear();
    m_activeAmbience.reset();
    m_prevAmbience.reset();

    // Destroy FX on submixes (XAudio2 takes ownership after SetEffectChain; but if we held IUnknown*, release to be safe)
#if CG_AUDIO_HAS_XAUDIO2FX
    for (int i = 0; i < kBusCount; ++i) {
        if (m_reverb[i].fx) { m_reverb[i].fx->Release(); m_reverb[i].fx = nullptr; }
        if (m_meter[i].fx)  { m_meter[i].fx->Release();  m_meter[i].fx  = nullptr; }
        m_reverb[i].enabled = false;
        m_meter[i].enabled  = false;
    }
#endif
#if CG_AUDIO_HAS_XAPOFX
    for (int i = 0; i < kBusCount; ++i) {
        if (m_eq[i].fx)   { m_eq[i].fx->Release();   m_eq[i].fx = nullptr; }
        if (m_echo[i].fx) { m_echo[i].fx->Release(); m_echo[i].fx = nullptr; }
        m_eq[i].enabled = false;
        m_echo[i].enabled = false;
    }
    if (m_masterLimiter) { m_masterLimiter->Release(); m_masterLimiter = nullptr; }
    m_masterLimiterEnabled = false;
#endif

    // Submix voices
    for (int i = 0; i < kBusCount; ++i) {
        if (m_submix[i]) {
            m_submix[i]->DestroyVoice();
            m_submix[i] = nullptr;
        }
    }

    if (m_master) {
        m_master->DestroyVoice();
        m_master = nullptr;
    }

    m_xaudio.Reset();
}

// --------------------------- Update/Transport/Scheduling ---------------------------

void XAudioEngine::Pause(bool pause) {
    if (m_paused == pause) return;
    m_paused = pause;
    for (auto& kv : m_voicesById) {
        if (!kv.second.voice) continue;
        if (pause) kv.second.voice->Stop(0);
        else       kv.second.voice->Start(0);
    }
}

bool XAudioEngine::IsPaused() const { return m_paused; }

uint64_t XAudioEngine::SchedulePlay(const std::string& eventName, double delaySec, float volumeScale, float pitchSemitoneOffset) {
    Scheduled s{};
    s.id = m_nextScheduleId++;
    s.triggerTime = m_timeSec + std::max(0.0, delaySec);
    s.eventName = eventName;
    s.volumeScale = volumeScale;
    s.pitchSemitones = pitchSemitoneOffset;
    // keep deque sorted by trigger time (stable insert)
    auto it = std::upper_bound(m_schedule.begin(), m_schedule.end(), s.triggerTime,
        [](double t, const Scheduled& a) { return t < a.triggerTime; });
    m_schedule.insert(it, std::move(s));
    return s.id;
}

void XAudioEngine::CancelScheduled(uint64_t scheduleId) {
    auto it = std::find_if(m_schedule.begin(), m_schedule.end(), [&](const Scheduled& s){ return s.id == scheduleId; });
    if (it != m_schedule.end()) m_schedule.erase(it);
}

void XAudioEngine::UpdateScheduling(double dt) {
    if (m_paused) return;
    m_timeSec += dt;
    while (!m_schedule.empty() && m_schedule.front().triggerTime <= m_timeSec) {
        Scheduled s = m_schedule.front();
        m_schedule.pop_front();

        auto evIt = m_events.find(s.eventName);
        if (evIt == m_events.end()) continue;

        if (s.emitter3D.has_value()) {
            Play3D(s.eventName, *s.emitter3D, s.volumeScale, s.pitchSemitones);
        } else {
            Play(s.eventName, s.volumeScale, s.pitchSemitones);
        }
    }
}

void XAudioEngine::Update(float dtSeconds) {
    UpdateScheduling(dtSeconds);

    // Tick fades / 3D / pan
    for (auto& kv : m_voicesById) {
        TickVoice(kv.second, dtSeconds);
    }

    // Ducking after per-voice volume set (applies to submixes)
    UpdateDucking(dtSeconds);

    // Reap finished voices and invoke callback if any
    ReapFinishedVoices();

    // If previous ambience finished, clear handle
    if (m_prevAmbience && !m_voicesById.count(m_prevAmbience->id)) {
        m_prevAmbience.reset();
    }
}

// --------------------------- Clip/Event Registry ---------------------------

bool XAudioEngine::RegisterClip(const std::string& id, const std::wstring& path) {
    if (id.empty()) return false;
    WavData wav{};
    if (!LoadWav(path, wav, nullptr)) return false;
    auto clip = std::make_shared<WavData>(std::move(wav));
    m_clips[id] = std::move(clip);
    return true;
}

void XAudioEngine::UnregisterClip(const std::string& id) {
    // NOTE: does not stop playing instances; call StopEvent before removing if needed
    m_clips.erase(id);
}

bool XAudioEngine::RegisterEvent(const std::string& name, const AudioEventDesc& desc) {
    if (name.empty()) return false;
    // Keep flexible: allow empty clip list for late binding (but Play will fail if unresolved)
    m_events[name] = desc;
    return true;
}

void XAudioEngine::UnregisterEvent(const std::string& name) {
    StopEvent(name, 0.05f);
    m_events.erase(name);
}

void XAudioEngine::PreloadEvent(const std::string& /*name*/) {
    // No-op for raw WAVs; placeholder for future streaming decode
}

// --------------------------- Playback ---------------------------

AudioEventHandle XAudioEngine::Play(const std::string& eventName,
                                    float volumeScale,
                                    float pitchSemitoneOffset) {
    auto it = m_events.find(eventName);
    if (it == m_events.end()) return {};
    auto* inst = PlayInternal(it->second, eventName, volumeScale, pitchSemitoneOffset, nullptr);
    return inst ? AudioEventHandle{ inst->id } : AudioEventHandle{};
}

AudioEventHandle XAudioEngine::Play3D(const std::string& eventName,
                                      const Emitter3D& emitter,
                                      float volumeScale,
                                      float pitchSemitoneOffset) {
    auto it = m_events.find(eventName);
    if (it == m_events.end()) return {};
#if CG_AUDIO_HAS_X3DAUDIO
    Ensure3DInitialized();
#endif
    auto* inst = PlayInternal(it->second, eventName, volumeScale, pitchSemitoneOffset, &emitter);
    return inst ? AudioEventHandle{ inst->id } : AudioEventHandle{};
}

void XAudioEngine::SetListener(const Listener3D& l) {
#if CG_AUDIO_HAS_X3DAUDIO
    m_listener = l;
#else
    (void)l;
#endif
}

Listener3D XAudioEngine::GetListener() const { return m_listener; }

void XAudioEngine::SetInstanceVolume(const AudioEventHandle& h, float linearVol) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it != m_voicesById.end() && it->second.voice) {
        it->second.volumeScale = std::max(0.0f, linearVol);
    }
}

void XAudioEngine::SetInstancePitchSemitones(const AudioEventHandle& h, float semitones) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end() || !it->second.voice) return;
    float ratio = SemitonesToRatio(semitones);
    ratio = std::clamp(ratio, kMinFreqRatio, kMaxFreqRatio); // SetFrequencyRatio bounds. :contentReference[oaicite:10]{index=10}
    it->second.voice->SetFrequencyRatio(ratio);
}

void XAudioEngine::SetInstance3D(const AudioEventHandle& h, const Emitter3D& emitter) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end()) return;
#if CG_AUDIO_HAS_X3DAUDIO
    it->second.is3D = true;
    it->second.emitter = emitter;
    Apply3DToVoice(it->second);
#else
    (void)emitter;
#endif
}

void XAudioEngine::SetInstancePan(const AudioEventHandle& h, float pan) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end()) return;
    it->second.pan = std::clamp(pan, -1.0f, 1.0f);
    ApplyPan(it->second); // uses SetOutputMatrix. :contentReference[oaicite:11]{index=11}
}

void XAudioEngine::SetInstanceSendLevel(const AudioEventHandle& h, AudioBus dstBus, float linear) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end()) return;
    auto& inst = it->second;
    if (!inst.voice) return;

    IXAudio2SubmixVoice* base = BusToSubmix(inst.bus);
    IXAudio2SubmixVoice* aux  = BusToSubmix(dstBus);
    if (!base || !aux) return;

    // Build send list with both base and aux
    XAUDIO2_SEND_DESCRIPTOR sends[2]{};
    sends[0].Flags = 0; sends[0].pOutputVoice = base;
    sends[1].Flags = 0; sends[1].pOutputVoice = aux;
    XAUDIO2_VOICE_SENDS vs{}; vs.SendCount = 2; vs.pSends = sends;

    inst.voice->SetOutputVoices(&vs);

    // Set matrices: unity to base, 'linear' to aux (rough, not per-channel)
    XAUDIO2_VOICE_DETAILS sd{}, bd{}, ad{};
    inst.voice->GetVoiceDetails(&sd);
    base->GetVoiceDetails(&bd);
    aux->GetVoiceDetails(&ad);

    std::vector<float> mBase(sd.InputChannels * bd.InputChannels, 1.0f);
    std::vector<float> mAux (sd.InputChannels * ad.InputChannels, linear);
    inst.voice->SetOutputMatrix(base, sd.InputChannels, bd.InputChannels, mBase.data());
    inst.voice->SetOutputMatrix(aux,  sd.InputChannels, ad.InputChannels, mAux.data());
}

void XAudioEngine::SetInstanceLowPass(const AudioEventHandle& h, float cutoffHz) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end() || !it->second.voice) return;

    // Use built-in one-pole conversion helper. :contentReference[oaicite:12]{index=12}
    XAUDIO2_VOICE_DETAILS vd{}; it->second.voice->GetVoiceDetails(&vd);
    XAUDIO2_FILTER_PARAMETERS fp{};
    if (cutoffHz <= 0.0f) {
        fp.Type = LowPassFilter; fp.Frequency = 0.0f; fp.OneOverQ = 1.0f;
    } else {
        fp.Type = LowPassFilter;
        fp.Frequency = XAudio2CutoffFrequencyToOnePoleCoefficient(cutoffHz, vd.InputSampleRate);
        fp.OneOverQ = 1.0f;
    }
    it->second.voice->SetFilterParameters(&fp); // requires voice created with USEFILTER. :contentReference[oaicite:13]{index=13}
}

void XAudioEngine::SetInstanceHighPass(const AudioEventHandle& h, float cutoffHz) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end() || !it->second.voice) return;

    XAUDIO2_VOICE_DETAILS vd{}; it->second.voice->GetVoiceDetails(&vd);
    XAUDIO2_FILTER_PARAMETERS fp{};
    if (cutoffHz <= 0.0f) {
        fp.Type = HighPassFilter; fp.Frequency = 0.0f; fp.OneOverQ = 1.0f;
    } else {
        fp.Type = HighPassFilter;
        fp.Frequency = XAudio2CutoffFrequencyToOnePoleCoefficient(cutoffHz, vd.InputSampleRate);
        fp.OneOverQ = 1.0f;
    }
    it->second.voice->SetFilterParameters(&fp);
}

void XAudioEngine::SetInstanceBandPass(const AudioEventHandle& h, float centerHz, float oneOverQ) {
    if (!h.Valid()) return;
    auto it = m_voicesById.find(h.id);
    if (it == m_voicesById.end() || !it->second.voice) return;

    XAUDIO2_VOICE_DETAILS vd{}; it->second.voice->GetVoiceDetails(&vd);
    XAUDIO2_FILTER_PARAMETERS fp{};
    if (centerHz <= 0.0f) {
        fp.Type = BandPassFilter; fp.Frequency = 0.0f; fp.OneOverQ = 1.0f;
    } else {
        fp.Type = BandPassFilter;
        fp.Frequency = XAudio2CutoffFrequencyToRadians(centerHz, vd.InputSampleRate); // radians for SVF. :contentReference[oaicite:14]{index=14}
        fp.OneOverQ = std::max(1e-3f, oneOverQ);
    }
    it->second.voice->SetFilterParameters(&fp);
}

void XAudioEngine::Stop(const AudioEventHandle& handle, float fadeOutSec) {
    if (!handle.Valid()) return;
    auto it = m_voicesById.find(handle.id);
    if (it == m_voicesById.end()) return;
    auto& inst = it->second;

    inst.fadeToSilenceThenStop = true;
    inst.fadeTime = std::max(0.0f, fadeOutSec);
    inst.fadeElapsed = 0.0f;
}

void XAudioEngine::StopEvent(const std::string& eventName, float fadeOutSec) {
    auto range = m_eventToVoiceIds.equal_range(eventName);
    std::vector<uint32_t> ids;
    for (auto it = range.first; it != range.second; ++it) ids.push_back(it->second);
    for (uint32_t id : ids) Stop(AudioEventHandle{id}, fadeOutSec);
}

void XAudioEngine::StopAll(float fadeOutSec) {
    for (auto& kv : m_voicesById) {
        Stop(AudioEventHandle{kv.first}, fadeOutSec);
    }
}

// --------------------------- Bus volumes / mutes / solos ---------------------------

static inline float EffectiveBusVol(float v, bool mute, bool anySolo, bool thisSolo) {
    if (mute) return 0.0f;
    if (anySolo && !thisSolo) return 0.0f;
    return v;
}

void XAudioEngine::SetBusVolume(AudioBus bus, float volume) {
    volume = Clamp(volume, 0.0f, 4.0f);
    if (bus == AudioBus::Master) {
        SetMasterVolume(volume);
        return;
    }
    m_busVol[(int)bus] = volume;

    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

float XAudioEngine::GetBusVolume(AudioBus bus) const {
    if (bus == AudioBus::Master) return m_masterVol;
    return m_busVol[(int)bus];
}

void XAudioEngine::SetMasterVolume(float volume) {
    m_masterVol = Clamp(volume, 0.0f, 4.0f);
    if (m_master) m_master->SetVolume(m_masterVol);
}

float XAudioEngine::GetMasterVolume() const {
    return m_masterVol;
}

void XAudioEngine::MuteBus(AudioBus bus, bool mute) {
    if (bus == AudioBus::Master) return;
    m_busMute[(int)bus] = mute;
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

bool XAudioEngine::IsBusMuted(AudioBus bus) const {
    if (bus == AudioBus::Master) return false;
    return m_busMute[(int)bus];
}

void XAudioEngine::SoloBus(AudioBus bus, bool solo) {
    if (bus == AudioBus::Master) return;
    m_busSolo[(int)bus] = solo;
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

bool XAudioEngine::IsBusSolo(AudioBus bus) const {
    if (bus == AudioBus::Master) return false;
    return m_busSolo[(int)bus];
}

// --------------------------- Bus FX (Reverb/Meter/EQ/Echo/Limiter) ---------------------------

// NOTE: Avoid referencing private nested types outside the class.
// Build the effect chain using generic flags + IUnknown* pointers, not slot type names.
#if CG_AUDIO_HAS_XAUDIO2FX || CG_AUDIO_HAS_XAPOFX
static void RebuildBusEffectChain(IXAudio2SubmixVoice* v,
#if CG_AUDIO_HAS_XAPOFX
    bool eqEnabled, IUnknown* eqFx,
    bool echoEnabled, IUnknown* echoFx,
#else
    bool /*eqEnabled*/, IUnknown* /*eqFx*/,
    bool /*echoEnabled*/, IUnknown* /*echoFx*/,
#endif
#if CG_AUDIO_HAS_XAUDIO2FX
    bool reverbEnabled, IUnknown* reverbFx,
    bool meterEnabled,  IUnknown* meterFx
#else
    bool /*reverbEnabled*/, IUnknown* /*reverbFx*/,
    bool /*meterEnabled*/,  IUnknown* /*meterFx*/
#endif
) {
    if (!v) return;

    std::vector<XAUDIO2_EFFECT_DESCRIPTOR> effs;
#if CG_AUDIO_HAS_XAPOFX
    if (eqEnabled  && eqFx)   { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = eqFx;   d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
    if (echoEnabled&& echoFx) { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = echoFx; d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
#endif
#if CG_AUDIO_HAS_XAUDIO2FX
    if (reverbEnabled && reverbFx) { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = reverbFx; d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
    if (meterEnabled  && meterFx)  { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = meterFx;  d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
#endif

    if (!effs.empty()) {
        XAUDIO2_EFFECT_CHAIN chain{ static_cast<UINT32>(effs.size()), effs.data() };
        v->SetEffectChain(&chain); // How-to: Create an Effect Chain. :contentReference[oaicite:15]{index=15}
    } else {
        // Clear chain
        XAUDIO2_EFFECT_CHAIN chain{ 0, nullptr };
        v->SetEffectChain(&chain);
    }
}
#endif

#if CG_AUDIO_HAS_XAUDIO2FX
void XAudioEngine::EnableBusReverb(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;

    if (enable && !m_reverb[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(XAudio2CreateReverb(&fx))) {       // XAudio2CreateReverb. :contentReference[oaicite:16]{index=16}
            m_reverb[(int)bus].fx = fx;
        }
    }
    m_reverb[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
#if CG_AUDIO_HAS_XAPOFX
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#else
        false, nullptr, false, nullptr,
#endif
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx);
}

void XAudioEngine::SetBusReverbParams(AudioBus bus, const XAUDIO2FX_REVERB_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_reverb[(int)bus];
    if (!v || !slot.fx) return;
    // Effect index depends on chain order; rebuild guarantees index:
    // EQ(0), Echo(1), Reverb(2), Meter(3). So reverb index is (#EQ + #Echo).
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    v->SetEffectParameters(idx, &p, sizeof(p));       // Reverb params; scale if needed by sample rate per docs. :contentReference[oaicite:17]{index=17}
}

bool XAudioEngine::GetBusReverbParams(AudioBus bus, XAUDIO2FX_REVERB_PARAMETERS& out) const {
    if (bus == AudioBus::Master) return false;
    auto* v = m_submix[(int)bus];
    auto& slot = m_reverb[(int)bus];
    if (!v || !slot.fx) return false;
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    return SUCCEEDED(v->GetEffectParameters(idx, &out, sizeof(out)));
}

void XAudioEngine::EnableBusMeter(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;

    if (enable && !m_meter[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(XAudio2CreateVolumeMeter(&fx, 0))) {  // VolumeMeter. :contentReference[oaicite:18]{index=18}
            m_meter[(int)bus].fx = fx;
        }
    }
    m_meter[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
#if CG_AUDIO_HAS_XAPOFX
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#else
        false, nullptr, false, nullptr,
#endif
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx);
}

bool XAudioEngine::GetBusMeterLevels(AudioBus bus, std::vector<float>& channelPeaks) const {
    if (bus == AudioBus::Master) return false;
    auto* v = m_submix[(int)bus];
    auto& slot = m_meter[(int)bus];
    if (!v || !slot.fx || !slot.enabled) return false;

    // Meter index is last in our chain.
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    if (m_reverb[(int)bus].enabled) ++idx;

    XAUDIO2_VOICE_DETAILS vd{};
    v->GetVoiceDetails(&vd);
    struct MeterLevels { float PeakLevel[XAUDIO2_MAX_AUDIO_CHANNELS]; float RMSLevel[XAUDIO2_MAX_AUDIO_CHANNELS]; };
    MeterLevels levels{};
    if (FAILED(v->GetEffectParameters(idx, &levels, sizeof(levels)))) return false;

    channelPeaks.assign(levels.PeakLevel, levels.PeakLevel + vd.InputChannels);
    return true;
}
#endif // CG_AUDIO_HAS_XAUDIO2FX

#if CG_AUDIO_HAS_XAPOFX
void XAudioEngine::EnableBusEQ(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;
    if (enable && !m_eq[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(CreateFX(__uuidof(FXEQ), &fx))) { // Create FXEQ. :contentReference[oaicite:19]{index=19}
            m_eq[(int)bus].fx = fx;
        }
    }
    m_eq[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#if CG_AUDIO_HAS_XAUDIO2FX
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx
#else
        false, nullptr, false, nullptr
#endif
    );
}

void XAudioEngine::SetBusEQParams(AudioBus bus, const FXEQ_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_eq[(int)bus];
    if (!v || !slot.fx) return;

    // EQ index at start of chain if enabled.
    UINT32 idx = 0;
    v->SetEffectParameters(idx, &p, sizeof(p));       // FXEQ params. :contentReference[oaicite:20]{index=20}
}

void XAudioEngine::EnableBusEcho(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;
    if (enable && !m_echo[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(CreateFX(__uuidof(FXEcho), &fx))) { // Create FXEcho. :contentReference[oaicite:21]{index=21}
            m_echo[(int)bus].fx = fx;
        }
    }
    m_echo[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#if CG_AUDIO_HAS_XAUDIO2FX
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx
#else
        false, nullptr, false, nullptr
#endif
    );
}

void XAudioEngine::SetBusEchoParams(AudioBus bus, const FXECHO_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_echo[(int)bus];
    if (!v || !slot.fx) return;

    // EQ may be at index 0; Echo is next if enabled.
    UINT32 idx = (m_eq[(int)bus].enabled ? 1u : 0u);
    v->SetEffectParameters(idx, &p, sizeof(p));
}

void XAudioEngine::EnableMasterLimiter(bool enable) {
    if (!m_master) return;
    if (enable == m_masterLimiterEnabled) return;

    if (enable) {
        if (!m_masterLimiter) {
            IUnknown* fx = nullptr;
            if (SUCCEEDED(CreateFX(__uuidof(FXMasteringLimiter), &fx))) { // FXMasteringLimiter. :contentReference[oaicite:22]{index=22}
                XAUDIO2_VOICE_DETAILS md{}; m_master->GetVoiceDetails(&md);
                XAUDIO2_EFFECT_DESCRIPTOR d{}; d.InitialState = TRUE; d.OutputChannels = md.InputChannels; d.pEffect = fx;
                XAUDIO2_EFFECT_CHAIN chain{1, &d};
                m_master->SetEffectChain(&chain);
                m_masterLimiter = fx;
                m_masterLimiterEnabled = true;
            }
        }
    } else {
        // Clear effects on master
        XAUDIO2_EFFECT_CHAIN chain{0, nullptr};
        m_master->SetEffectChain(&chain);
        if (m_masterLimiter) { m_masterLimiter->Release(); m_masterLimiter = nullptr; }
        m_masterLimiterEnabled = false;
    }
}
#endif // CG_AUDIO_HAS_XAPOFX

// --------------------------- Ambience ---------------------------

void XAudioEngine::RegisterAmbience(Biome biome, Climate climate, const std::string& eventName) {
    m_ambienceMap[{biome, climate}] = eventName;
}

void XAudioEngine::ClearAmbienceMap() {
    m_ambienceMap.clear();
}

void XAudioEngine::SetAmbience(Biome biome, Climate climate, float crossfadeSec) {
    auto it = m_ambienceMap.find({biome, climate});
    if (it == m_ambienceMap.end()) return;
    SetAmbienceByEvent(it->second, crossfadeSec);
}

void XAudioEngine::SetAmbienceByEvent(const std::string& eventName, float crossfadeSec) {
    // If already active with same event, ignore
    if (m_activeAmbience && m_voicesById.count(m_activeAmbience->id)) {
        auto& inst = m_voicesById[m_activeAmbience->id];
        if (inst.eventName == eventName) return;
    }

    // Start new ambience (must be registered & looping)
    auto evIt = m_events.find(eventName);
    if (evIt == m_events.end()) return;

    AudioEventDesc desc = evIt->second;
    desc.bus = AudioBus::Ambience;
    desc.loop = true;
    desc.fades.inSec = std::max(0.0f, crossfadeSec);

    auto* newInst = PlayInternal(desc, eventName, 1.0f, 0.0f, nullptr);
    if (!newInst) return;

    // Fade out previous ambience
    if (m_activeAmbience && m_voicesById.count(m_activeAmbience->id)) {
        auto prev = *m_activeAmbience;
        Stop(prev, crossfadeSec);
        m_prevAmbience = prev;
    } else {
        m_prevAmbience.reset();
    }

    m_activeAmbience = AudioEventHandle{newInst->id};
}

// --------------------------- Snapshots / RTPCs / Occlusion ---------------------------

XAudioEngine::Snapshot XAudioEngine::CaptureSnapshot() const {
    Snapshot s{};
    for (int i = 0; i < kBusCount; ++i) {
        s.busVolumes[i] = m_busVol[i];
        s.busMutes[i]   = m_busMute[i];
        s.busSolos[i]   = m_busSolo[i];
    }
    return s;
}

void XAudioEngine::ApplySnapshot(const Snapshot& s, float /*fadeSec*/) {
    for (int i = 0; i < kBusCount; ++i) {
        m_busVol[i]  = Clamp(s.busVolumes[i], 0.0f, 4.0f);
        m_busMute[i] = s.busMutes[i];
        m_busSolo[i] = s.busSolos[i];
    }
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool v){return v;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

void XAudioEngine::SetRTPC(const std::string& name, std::function<void(float)> fn) {
    if (name.empty()) return;
    m_rtpcs[name] = std::move(fn);
}

void XAudioEngine::RemoveRTPC(const std::string& name) {
    m_rtpcs.erase(name);
}

void XAudioEngine::UpdateRTPC(const std::string& name, float value) {
    auto it = m_rtpcs.find(name);
    if (it != m_rtpcs.end() && it->second) it->second(value);
}

void XAudioEngine::ClearRTPCs() {
    m_rtpcs.clear();
}

void XAudioEngine::SetOcclusionMapping(float minCutoffHz, float maxCutoffHz, float minGainLinear, float maxGainLinear) {
    m_occMinCutHz = std::max(10.0f, minCutoffHz);
    m_occMaxCutHz = std::max(m_occMinCutHz, maxCutoffHz);
    m_occMinGain  = Clamp(minGainLinear, 0.0f, 1.0f);
    m_occMaxGain  = Clamp(maxGainLinear, 0.0f, 1.0f);
    if (m_occMaxGain < m_occMinGain) std::swap(m_occMaxGain, m_occMinGain);
}

// --------------------------- Queries / Callbacks ---------------------------

uint32_t XAudioEngine::GetActiveInstanceCount(const std::string& eventName) const {
    auto range = m_eventToVoiceIds.equal_range(eventName);
    return static_cast<uint32_t>(std::distance(range.first, range.second));
}

uint32_t XAudioEngine::GetTotalActiveInstances() const {
    return static_cast<uint32_t>(m_voicesById.size());
}

PerformanceData XAudioEngine::GetPerformanceData() const {
    PerformanceData out{};
    if (!m_xaudio) return out;

    XAUDIO2_PERFORMANCE_DATA pd{};
    m_xaudio->GetPerformanceData(&pd); // GetPerformanceData. :contentReference[oaicite:23]{index=23}

    // Fields available in XAudio2 2.9 (Windows 10/11 SDK). 'TotalVoiceCount' no longer exists.
    out.activeSourceVoiceCount    = pd.ActiveSourceVoiceCount;
    // Derive an approximate "total voices" = sources + submixes + master
    uint32_t derivedTotalVoices   = pd.ActiveSourceVoiceCount + pd.ActiveSubmixVoiceCount + 1u;
    out.totalVoices               = derivedTotalVoices;

    out.audioCyclesSinceLastQuery = pd.AudioCyclesSinceLastQuery;
    out.totalCyclesSinceLastQuery = pd.TotalCyclesSinceLastQuery;
    out.memoryUsageBytes          = pd.MemoryUsageInBytes;
    out.currentLatencySamples     = pd.CurrentLatencyInSamples;
    return out;
}

void XAudioEngine::SetOnEventEnd(OnEventEnd cb) { m_onEventEnd = std::move(cb); }

// --------------------------- Internals ---------------------------

XAudioEngine::VoiceInstance* XAudioEngine::PlayInternal(const AudioEventDesc& desc,
                                                        const std::string& eventName,
                                                        float volumeScale,
                                                        float pitchSemitoneOffset,
                                                        const Emitter3D* emitterOpt) {
    if (!m_master) return nullptr;

    // Enforce polyphony/steal policy
    uint32_t stolenId = 0;
    if (!EnforcePolyphony(eventName, desc, stolenId) && stolenId == 0) {
        // If None policy and max reached, refuse
        auto range = m_eventToVoiceIds.equal_range(eventName);
        size_t count = std::distance(range.first, range.second);
        if (count >= desc.maxPolyphony && desc.steal == VoiceStealPolicy::None) return nullptr;
    }

    // Choose a clip (weighted if choices present)
    std::string clipId;
    RangeF volJ = desc.volumeJitter;
    RangeF pitJ = desc.pitchSemitoneJitter;
    float startOffsetSec = 0.0f;
    if (!desc.choices.empty()) {
        float totalW = 0.0f;
        for (auto& c : desc.choices) totalW += std::max(0.0f, c.weight);
        if (totalW <= 0.0f) return nullptr;
        float r = RandRange(m_rng, 0.0f, totalW);
        float acc = 0.0f;
        for (auto& c : desc.choices) {
            acc += std::max(0.0f, c.weight);
            if (r <= acc) {
                clipId = c.clipId;
                volJ   = c.volumeJitter;
                pitJ   = c.pitchSemitoneJitter;
                startOffsetSec = std::max(0.0f, c.startOffsetSec);
                break;
            }
        }
    } else {
        if (desc.clipIds.empty()) return nullptr;
        if (desc.clipIds.size() == 1) clipId = desc.clipIds.front();
        else {
            size_t idx = static_cast<size_t>(RandRange(m_rng, 0.0f, float(desc.clipIds.size() - 1e-3f)));
            clipId = desc.clipIds[idx];
        }
    }

    auto clipIt = m_clips.find(clipId);
    if (clipIt == m_clips.end()) return nullptr;
    ClipPtr clip = clipIt->second;
    const WAVEFORMATEX* wfx = clip->Wfx();

    // Sends: route to the selected bus
    IXAudio2SubmixVoice* busVoice = BusToSubmix(desc.bus);
    if (!busVoice) busVoice = BusToSubmix(AudioBus::SFX);

    XAUDIO2_SEND_DESCRIPTOR sendDesc{};
    sendDesc.Flags = 0;
    sendDesc.pOutputVoice = busVoice;

    XAUDIO2_VOICE_SENDS sends{};
    sends.SendCount = 1;
    sends.pSends = &sendDesc;

    IXAudio2SourceVoice* sv = nullptr;
    // Enable per-voice filters with USEFILTER (required for SetFilterParameters). :contentReference[oaicite:24]{index=24}
    HRESULT hr = m_xaudio->CreateSourceVoice(&sv, wfx, XAUDIO2_VOICE_USEFILTER, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends, nullptr);
    if (FAILED(hr) || !sv) return nullptr;

    // Build buffer (single submission; loop if needed)
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = static_cast<UINT32>(clip->samples.size());
    buf.pAudioData = clip->samples.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (desc.loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
        buf.LoopBegin = 0;
        buf.LoopLength = 0;
    }
    if (startOffsetSec > 0.0f && clip->sampleBytesPerFrame > 0 && wfx->nSamplesPerSec > 0) {
        const uint32_t framesPerSec = wfx->nSamplesPerSec;
        const uint32_t framesOffset = static_cast<uint32_t>(std::min<uint64_t>(
            uint64_t(startOffsetSec * framesPerSec),
            uint64_t(clip->samples.size() / clip->sampleBytesPerFrame)));
        buf.PlayBegin  = framesOffset;
        buf.PlayLength = 0; // play to end (or loop)
    }

    hr = sv->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        sv->DestroyVoice();
        return nullptr;
    }

    // Randomize volume & pitch
    float volMul = Clamp(RandRange(m_rng, volJ.min, volJ.max), 0.0f, 16.0f);
    float semis  = Clamp(RandRange(m_rng, pitJ.min, pitJ.max) + pitchSemitoneOffset, -48.0f, 48.0f);
    float ratio  = SemitonesToRatio(semis);
    ratio = std::clamp(ratio, kMinFreqRatio, kMaxFreqRatio); // CreateSourceVoice MaxFrequencyRatio / SetFrequencyRatio. :contentReference[oaicite:25]{index=25}
    sv->SetFrequencyRatio(ratio);

    // Allocate instance id before start
    uint32_t id = m_nextId++;

    // Create instance record
    VoiceInstance inst{};
    inst.id = id;
    inst.voice = sv;
    inst.clip = clip;
    inst.eventName = eventName;
    inst.baseVolume = desc.baseVolume * volMul;
    inst.volumeScale = volumeScale;
    inst.currentVol = (desc.fades.inSec > 0.0f || desc.startDelaySec > 0.0f) ? 0.0f : 1.0f;
    inst.targetVol = 1.0f;
    inst.fadeTime = desc.fades.inSec + desc.startDelaySec; // simple delay+fade behavior
    inst.fadeElapsed = 0.0f;
    inst.fadeToSilenceThenStop = false;
    inst.looping = desc.loop;
    inst.bus = desc.bus;
    inst.is3D = (emitterOpt != nullptr);
    if (inst.is3D) {
        inst.emitter = *emitterOpt;
        inst.distanceModel = desc.distanceModel;
    }

    // Apply initial volume before start
    sv->SetVolume(inst.currentVol * inst.baseVolume * inst.volumeScale);
    hr = sv->Start(0);
    if (FAILED(hr)) {
        sv->DestroyVoice();
        return nullptr;
    }

    // Track
    m_voicesById.emplace(inst.id, inst);
    m_eventToVoiceIds.emplace(eventName, inst.id);

#if CG_AUDIO_HAS_X3DAUDIO
    if (inst.is3D) Apply3DToVoice(m_voicesById.find(inst.id)->second);
#endif
    ApplyPan(m_voicesById.find(inst.id)->second);

    return &m_voicesById.find(inst.id)->second;
}

void XAudioEngine::DestroyVoice(VoiceInstance& inst) {
    if (inst.voice) {
        inst.voice->Stop(0);
        inst.voice->FlushSourceBuffers();
        inst.voice->DestroyVoice();
        inst.voice = nullptr;
    }
}

void XAudioEngine::TickVoice(VoiceInstance& inst, float dt) {
    if (!inst.voice) return;

    // Fades (linear across fadeTime)
    if (inst.fadeTime > 0.0f) {
        inst.fadeElapsed += dt;
        float t = Clamp(inst.fadeElapsed / inst.fadeTime, 0.0f, 1.0f);
        float goal = inst.fadeToSilenceThenStop ? 0.0f : inst.targetVol;
        inst.currentVol = Lerp(inst.currentVol, goal, t);
        if (inst.fadeElapsed >= inst.fadeTime) {
            inst.fadeTime = 0.0f;
            inst.fadeElapsed = 0.0f;

            if (inst.fadeToSilenceThenStop) {
                DestroyVoice(inst);
                return;
            }
        }
    }

#if CG_AUDIO_HAS_X3DAUDIO
    if (inst.is3D) {
        Apply3DToVoice(inst); // update doppler + matrix as emitter/listener moves
    }
#endif

    // Apply occlusion as a simple LPF + gain scaling
    ApplyOcclusion(inst);

    // Pan (2D)
    ApplyPan(inst);

    // Apply composite gain
    if (inst.voice) {
        inst.voice->SetVolume(inst.currentVol * inst.baseVolume * inst.volumeScale);
    }
}

void XAudioEngine::ApplyFade(VoiceInstance& /*inst*/, float /*dt*/) {
    // handled inline in TickVoice (kept for API symmetry)
}

void XAudioEngine::ApplyPan(VoiceInstance& inst) {
    if (!inst.voice) return;
    IXAudio2SubmixVoice* bus = BusToSubmix(inst.bus);
    if (!bus) return;

    XAUDIO2_VOICE_DETAILS sd{}, dd{};
    inst.voice->GetVoiceDetails(&sd);
    bus->GetVoiceDetails(&dd);

    if (dd.InputChannels < 2) return; // nothing to pan to

    std::vector<float> mat;
    BuildStereoPanMatrix(inst.pan, sd.InputChannels, dd.InputChannels, mat);
    if (!mat.empty()) {
        inst.voice->SetOutputMatrix(bus, sd.InputChannels, dd.InputChannels, mat.data()); // SetOutputMatrix. :contentReference[oaicite:26]{index=26}
    }
}

void XAudioEngine::ReapFinishedVoices() {
    std::vector<uint32_t> dead;
    dead.reserve(16);

    for (auto& kv : m_voicesById) {
        auto& inst = kv.second;
        if (!inst.voice) {
            dead.push_back(kv.first);
            continue;
        }
        XAUDIO2_VOICE_STATE st{};
        inst.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        // Finished if not looping and no buffers queued (we submit only one buffer)
        if (!inst.looping && st.BuffersQueued == 0) {
            DestroyVoice(inst);
            dead.push_back(kv.first);
        }
    }

    for (uint32_t id : dead) {
        auto it = m_voicesById.find(id);
        if (it != m_voicesById.end()) {
            // callback before unlink
            if (m_onEventEnd) {
                AudioEventHandle h{it->second.id};
                m_onEventEnd(h, it->second.eventName);
            }
            // unlink from event map
            auto range = m_eventToVoiceIds.equal_range(it->second.eventName);
            for (auto mit = range.first; mit != range.second; ) {
                if (mit->second == id) mit = m_eventToVoiceIds.erase(mit);
                else ++mit;
            }
            m_voicesById.erase(it);
        }
    }
}

IXAudio2SubmixVoice* XAudioEngine::BusToSubmix(AudioBus bus) const {
    if (bus == AudioBus::Master) return nullptr; // source voices don't output directly to Master
    return m_submix[(int)bus];
}

// --------------------------- 3D helpers ---------------------------

void XAudioEngine::Ensure3DInitialized() {
#if CG_AUDIO_HAS_X3DAUDIO
    if (m_masterChannelMask == 0 && m_master) {
        DWORD chmask = 0;
        if (SUCCEEDED(m_master->GetChannelMask(&chmask))) { // GetChannelMask. :contentReference[oaicite:27]{index=27}
            m_masterChannelMask = chmask;
            X3DAudioInitialize(m_masterChannelMask, m_speedOfSound, m_x3dInstance); // X3DAudioInitialize. :contentReference[oaicite:28]{index=28}
        }
    }
#endif
}

void XAudioEngine::Apply3DToVoice(VoiceInstance& inst) {
#if CG_AUDIO_HAS_X3DAUDIO
    if (!inst.voice) return;
    IXAudio2SubmixVoice* bus = BusToSubmix(inst.bus);
    if (!bus) return;

    XAUDIO2_VOICE_DETAILS src{}, dst{};
    inst.voice->GetVoiceDetails(&src);
    bus->GetVoiceDetails(&dst);

    X3DAUDIO_LISTENER lis{};
    lis.pCone = nullptr;
    lis.OrientFront = { m_listener.orientation.forward.x, m_listener.orientation.forward.y, m_listener.orientation.forward.z };
    lis.OrientTop   = { m_listener.orientation.up.x,      m_listener.orientation.up.y,      m_listener.orientation.up.z };
    lis.Position    = { m_listener.position.x, m_listener.position.y, m_listener.position.z };
    lis.Velocity    = { m_listener.velocity.x, m_listener.velocity.y, m_listener.velocity.z };

    X3DAUDIO_EMITTER em{};
    em.pCone = nullptr;
    em.OrientFront = { inst.emitter.orientation.forward.x, inst.emitter.orientation.forward.y, inst.emitter.orientation.forward.z };
    em.OrientTop   = { inst.emitter.orientation.up.x,      inst.emitter.orientation.up.y,      inst.emitter.orientation.up.z };
    em.Position    = { inst.emitter.position.x, inst.emitter.position.y, inst.emitter.position.z };
    em.Velocity    = { inst.emitter.velocity.x, inst.emitter.velocity.y, inst.emitter.velocity.z };
    em.ChannelCount = src.InputChannels;
    em.InnerRadius  = inst.emitter.innerRadius;
    em.InnerRadiusAngle = inst.emitter.innerRadiusAngle;
    em.CurveDistanceScaler = 1.0f;
    em.DopplerScaler = m_listener.dopplerScalar * inst.emitter.dopplerScalar;

    std::vector<float> matrix(src.InputChannels * dst.InputChannels, 0.0f);
    X3DAUDIO_DSP_SETTINGS dsp{};
    dsp.SrcChannelCount = src.InputChannels;
    dsp.DstChannelCount = dst.InputChannels;
    dsp.pMatrixCoefficients = matrix.data();

    DWORD flags = X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER;
    X3DAudioCalculate(m_x3dInstance, &lis, &em, flags, &dsp); // Integrate X3DAudio w/ XAudio2. :contentReference[oaicite:29]{index=29}

    // Apply doppler
    float ratio = std::clamp(dsp.DopplerFactor, kMinFreqRatio, kMaxFreqRatio);
    inst.voice->SetFrequencyRatio(ratio);

    // Apply panning/attenuation matrix
    inst.voice->SetOutputMatrix(bus, src.InputChannels, dst.InputChannels, matrix.data());
#else
    (void)inst;
#endif
}

// --------------------------- WAV Loader ---------------------------

#pragma pack(push, 1)
struct RiffHeader { uint32_t riff; uint32_t size; uint32_t wave; };
struct ChunkHeader { uint32_t id; uint32_t size; };
#pragma pack(pop)

bool XAudioEngine::LoadWav(const std::wstring& path, WavData& out, std::string* outErr) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (outErr) *outErr = "Failed to open WAV file";
        return false;
    }

    RiffHeader rh{};
    f.read(reinterpret_cast<char*>(&rh), sizeof(rh));
    const uint32_t RIFF = MakeTag('R','I','F','F');
    const uint32_t WAVE = MakeTag('W','A','V','E');
    if (!f || rh.riff != RIFF || rh.wave != WAVE) {
        if (outErr) *outErr = "Not a RIFF/WAVE file";
        return false;
    }

    bool haveFmt = false;
    bool haveData = false;
    std::vector<uint8_t> fmtBuf;
    std::vector<uint8_t> dataBuf;

    while (f && (!haveFmt || !haveData)) {
        ChunkHeader ch{};
        f.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        if (!f) break;

        if (ch.id == MakeTag('f','m','t',' ')) {
            fmtBuf.resize(ch.size);
            f.read(reinterpret_cast<char*>(fmtBuf.data()), ch.size);
            haveFmt = true;
        } else if (ch.id == MakeTag('d','a','t','a')) {
            dataBuf.resize(ch.size);
            f.read(reinterpret_cast<char*>(dataBuf.data()), ch.size);
            haveData = true;
        } else {
            // Skip unknown chunk
            f.seekg(ch.size, std::ios::cur);
        }

        // Chunks are word-aligned
        if (ch.size & 1) f.seekg(1, std::ios::cur);
    }

    if (!haveFmt || !haveData) {
        if (outErr) *outErr = "Missing fmt/data chunk";
        return false;
    }

    if (fmtBuf.size() < sizeof(WAVEFORMATEX)) {
        if (outErr) *outErr = "fmt chunk too small";
        return false;
    }

    // Parse format
    const WAVEFORMATEX* wfx = reinterpret_cast<const WAVEFORMATEX*>(fmtBuf.data());
    out.sampleBytesPerFrame = wfx->nBlockAlign;

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmtBuf.size() >= sizeof(WAVEFORMATEXTENSIBLE)) {
        out.isExtensible = true;
        std::memset(&out.fmtExt, 0, sizeof(out.fmtExt));
        std::memcpy(&out.fmtExt, fmtBuf.data(), sizeof(WAVEFORMATEXTENSIBLE));
    } else {
        out.isExtensible = false;
        std::memset(&out.fmtExt, 0, sizeof(out.fmtExt));
        std::memcpy(&out.fmtExt, fmtBuf.data(), sizeof(WAVEFORMATEX)); // copy header fields
        out.fmtExt.Samples.wValidBitsPerSample = wfx->wBitsPerSample;   // best-effort
    }

    out.samples = std::move(dataBuf);
    return true;
}

// --------------------------- Polyphony / Ducking / Filters ---------------------------

bool XAudioEngine::EnforcePolyphony(const std::string& eventName, const AudioEventDesc& desc, uint32_t& outStolenId) {
    outStolenId = 0;
    auto range = m_eventToVoiceIds.equal_range(eventName);
    std::vector<uint32_t> ids;
    for (auto it = range.first; it != range.second; ++it) ids.push_back(it->second);
    if (ids.size() < desc.maxPolyphony) return true;

    if (desc.steal == VoiceStealPolicy::None) return false;

    // Choose a victim
    uint32_t victim = 0;
    switch (desc.steal) {
        case VoiceStealPolicy::Oldest: {
            victim = *std::min_element(ids.begin(), ids.end()); // lower id ~ older
        } break;
        case VoiceStealPolicy::Newest: {
            victim = *std::max_element(ids.begin(), ids.end());
        } break;
        case VoiceStealPolicy::Quietest: {
            float minVol = std::numeric_limits<float>::max();
            for (uint32_t id : ids) {
                auto it = m_voicesById.find(id);
                if (it == m_voicesById.end()) continue;
                float v = it->second.currentVol * it->second.baseVolume * it->second.volumeScale;
                if (v < minVol) { minVol = v; victim = id; }
            }
        } break;
        default: break;
    }

    if (victim != 0) {
        auto it = m_voicesById.find(victim);
        if (it != m_voicesById.end()) {
            DestroyVoice(it->second);
            // unlink from event map
            auto r = m_eventToVoiceIds.equal_range(eventName);
            for (auto mit = r.first; mit != r.second; ) {
                if (mit->second == victim) mit = m_eventToVoiceIds.erase(mit);
                else ++mit;
            }
            m_voicesById.erase(it);
            outStolenId = victim;
            return true;
        }
    }
    return false;
}

void XAudioEngine::UpdateDucking(float dt) {
    if (m_duckRules.empty()) return;

    // Simple activity heuristic: if any voice exists on 'ducker' bus, target=1 else 0; attack/release smoothing.
    bool busActive[kBusCount] = {false,false,false,false};
    for (auto& kv : m_voicesById) {
        if (!kv.second.voice) continue;
        busActive[(int)kv.second.bus] = true;
    }

    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});

    for (auto& rule : m_duckRules) {
        float target = busActive[(int)rule.ducker] ? 1.0f : 0.0f;
        float tau = (target > rule.env) ? std::max(1e-4f, rule.attackSec) : std::max(1e-4f, rule.releaseSec);
        float k = Clamp(dt / tau, 0.0f, 1.0f);
        rule.env = Lerp(rule.env, target, k);

        float duckGain = DbToLin(-std::abs(rule.attenDb) * rule.env);
        int duckedIdx = (int)rule.ducked;

        // Apply combined bus volume with ducking
        if (m_submix[duckedIdx]) {
            float eff = EffectiveBusVol(m_busVol[duckedIdx], m_busMute[duckedIdx], anySolo, m_busSolo[duckedIdx]);
            m_submix[duckedIdx]->SetVolume(eff * duckGain);
        }
    }
}

void XAudioEngine::SetVoiceFilter(IXAudio2SourceVoice* v, XAUDIO2_FILTER_TYPE type, float cutoffHz, float oneOverQ) {
    if (!v) return;
    XAUDIO2_VOICE_DETAILS vd{}; v->GetVoiceDetails(&vd);
    XAUDIO2_FILTER_PARAMETERS fp{};
    fp.Type = type;
    if (type == LowPassFilter || type == HighPassFilter) {
        fp.Frequency = (cutoffHz <= 0.0f) ? 0.0f : XAudio2CutoffFrequencyToOnePoleCoefficient(cutoffHz, vd.InputSampleRate); // one-pole helper. :contentReference[oaicite:30]{index=30}
        fp.OneOverQ = 1.0f;
    } else {
        fp.Frequency = (cutoffHz <= 0.0f) ? 0.0f : XAudio2CutoffFrequencyToRadians(cutoffHz, vd.InputSampleRate);            // radians helper for SVF. :contentReference[oaicite:31]{index=31}
        fp.OneOverQ = std::max(1e-3f, oneOverQ);
    }
    v->SetFilterParameters(&fp);
}

void XAudioEngine::ApplyOcclusion(VoiceInstance& inst) {
    if (!inst.voice || !inst.is3D) return;
    float occ = Clamp01(inst.emitter.occlusion + inst.emitter.obstruction);
    if (occ <= 0.0f) {
        // Clear LPF
        SetVoiceFilter(inst.voice, LowPassFilter, 0.0f, 1.0f);
        return;
    }
    // Map to cutoff & gain
    float cut = Lerp(m_occMaxCutHz, m_occMinCutHz, occ);
    float g   = Lerp(m_occMaxGain,  m_occMinGain,  occ);
    SetVoiceFilter(inst.voice, LowPassFilter, cut, 1.0f);
    inst.volumeScale = std::max(0.0f, g); // apply gain scaling via volumeScale (transient)
}

// --------------------------- Ducking API ---------------------------

void XAudioEngine::AddDuckingRule(AudioBus ducked, AudioBus ducker, float attenuationDb, float attackSec, float releaseSec) {
    m_duckRules.push_back(DuckRule{ ducked, ducker, attenuationDb, attackSec, releaseSec, 0.0f });
}

void XAudioEngine::ClearDuckingRules() {
    m_duckRules.clear();
}

} // namespace colony::audio
