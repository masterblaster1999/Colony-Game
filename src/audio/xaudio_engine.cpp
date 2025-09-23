// xaudio_engine.cpp
#include "xaudio_engine.h"

#include <fstream>
#include <cstring>
#include <cassert>

using Microsoft::WRL::ComPtr;

namespace colony::audio {

// ============================ Helpers ============================

static inline float RandRange(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

// Chunk tag helpers
static uint32_t MakeTag(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

// ============================ XAudioEngine ============================

XAudioEngine::~XAudioEngine() {
    Shutdown();
}

bool XAudioEngine::Init() {
#if defined(_DEBUG)
    UINT32 flags = XAUDIO2_DEBUG_ENGINE;
#else
    UINT32 flags = 0;
#endif

    HRESULT hr = XAudio2Create(m_xaudio.GetAddressOf(), flags, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !m_xaudio) return false;

    hr = m_xaudio->CreateMasteringVoice(&m_master);
    if (FAILED(hr) || !m_master) return false;

    // Create submix voices for buses (route to master by default)
    XAUDIO2_VOICE_DETAILS masterDetails{};
    m_master->GetVoiceDetails(&masterDetails);
    const UINT32 channels = masterDetails.InputChannels;
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

    // Initialize volumes
    m_masterVol = 1.0f;
    m_master->SetVolume(m_masterVol);
    m_submix[(int)AudioBus::SFX]->SetVolume(1.0f);
    m_submix[(int)AudioBus::Music]->SetVolume(1.0f);
    m_submix[(int)AudioBus::Ambience]->SetVolume(1.0f);

    return true;
}

void XAudioEngine::Shutdown() {
    // Stop and destroy all voices first
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

    // Submix voices
    for (int i = 0; i < (int)AudioBus::COUNT; ++i) {
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

void XAudioEngine::Update(float dtSeconds) {
    // Tick fades, reap finished
    std::vector<uint32_t> toErase;
    toErase.reserve(m_voicesById.size());

    for (auto& kv : m_voicesById) {
        TickVoice(kv.second, dtSeconds);
    }

    ReapFinishedVoices();

    // If previous ambience finished, clear handle
    if (m_prevAmbience && !m_voicesById.count(m_prevAmbience->id)) {
        m_prevAmbience.reset();
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
            inst.voice->DestroyVoice();
            inst.voice = nullptr;
            dead.push_back(kv.first);
        }
    }

    for (uint32_t id : dead) {
        auto it = m_voicesById.find(id);
        if (it != m_voicesById.end()) {
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

void XAudioEngine::TickVoice(VoiceInstance& inst, float dt) {
    if (!inst.voice) return;

    // Fades (simple linear)
    if (inst.fadeTime > 0.0f) {
        inst.fadeElapsed += dt;
        float t = Clamp(inst.fadeElapsed / inst.fadeTime, 0.0f, 1.0f);
        float goal = inst.fadeToSilenceThenStop ? 0.0f : inst.targetVol;
        inst.currentVol = Lerp(inst.currentVol, goal, t);
        inst.voice->SetVolume(inst.currentVol * inst.baseVolume * inst.volumeScale);

        if (inst.fadeElapsed >= inst.fadeTime) {
            inst.fadeTime = 0.0f;
            inst.fadeElapsed = 0.0f;

            if (inst.fadeToSilenceThenStop) {
                inst.voice->Stop(0);
                inst.voice->FlushSourceBuffers();
                inst.voice->DestroyVoice();
                inst.voice = nullptr;
            }
        }
    }
    else {
        // Keep applied
        inst.voice->SetVolume(inst.currentVol * inst.baseVolume * inst.volumeScale);
    }
}

bool XAudioEngine::RegisterClip(const std::string& id, const std::wstring& path) {
    if (id.empty()) return false;
    WavData wav{};
    if (!LoadWav(path, wav, nullptr)) return false;
    auto clip = std::make_shared<WavData>(std::move(wav));
    m_clips[id] = std::move(clip);
    return true;
}

bool XAudioEngine::RegisterEvent(const std::string& name, const AudioEventDesc& desc) {
    if (name.empty() || desc.clipIds.empty()) return false;
    // Ensure clips exist (soft check; allows late registration too)
    m_events[name] = desc;
    return true;
}

void XAudioEngine::UnregisterClip(const std::string& id) {
    // NOTE: does not stop playing instances; call StopEvent before removing if needed
    m_clips.erase(id);
}

void XAudioEngine::UnregisterEvent(const std::string& name) {
    StopEvent(name, 0.05f);
    m_events.erase(name);
}

AudioEventHandle XAudioEngine::Play(const std::string& eventName,
                                    float volumeScale,
                                    float pitchSemitoneOffset) {
    auto it = m_events.find(eventName);
    if (it == m_events.end()) return {};

    auto* inst = PlayInternal(it->second, eventName, volumeScale, pitchSemitoneOffset);
    return inst ? AudioEventHandle{ inst->id } : AudioEventHandle{};
}

XAudioEngine::VoiceInstance* XAudioEngine::PlayInternal(const AudioEventDesc& desc,
                                                        const std::string& eventName,
                                                        float volumeScale,
                                                        float pitchSemitoneOffset) {
    if (!m_master) return nullptr;

    // Enforce polyphony (drop newest if limit reached)
    {
        auto range = m_eventToVoiceIds.equal_range(eventName);
        size_t count = std::distance(range.first, range.second);
        if (count >= desc.maxPolyphony) {
            // Optional: steal oldest by stopping immediately
            // Here we simply refuse to spawn another
            return nullptr;
        }
    }

    // Choose a clip
    if (desc.clipIds.empty()) return nullptr;
    const std::string& clipId =
        desc.clipIds.size() == 1
            ? desc.clipIds.front()
            : desc.clipIds[(size_t)RandRange(m_rng, 0.0f, float(desc.clipIds.size() - 1e-3f))];

    auto clipIt = m_clips.find(clipId);
    if (clipIt == m_clips.end()) return nullptr;
    ClipPtr clip = clipIt->second;
    const WAVEFORMATEX* wfx = clip->Wfx();

    // Prepare sends: route to the right bus submix
    IXAudio2SubmixVoice* busVoice = BusToSubmix(desc.bus);
    if (!busVoice) busVoice = BusToSubmix(AudioBus::SFX);

    XAUDIO2_SEND_DESCRIPTOR sendDesc{};
    sendDesc.Flags = 0;
    sendDesc.pOutputVoice = busVoice;

    XAUDIO2_VOICE_SENDS sends{};
    sends.SendCount = 1;
    sends.pSends = &sendDesc;

    IXAudio2SourceVoice* sv = nullptr;
    HRESULT hr = m_xaudio->CreateSourceVoice(&sv, wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends, nullptr);
    if (FAILED(hr) || !sv) return nullptr;

    // Build buffer (one submission; use XAUDIO2_LOOP_INFINITE for looping)
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = static_cast<UINT32>(clip->samples.size());
    buf.pAudioData = clip->samples.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (desc.loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
        buf.LoopBegin = 0;
        buf.LoopLength = 0;
    }

    hr = sv->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        sv->DestroyVoice();
        return nullptr;
    }

    // Randomize volume & pitch
    float volMul = Clamp(RandRange(m_rng, desc.volumeJitter.min, desc.volumeJitter.max), 0.0f, 16.0f);
    float semis  = Clamp(RandRange(m_rng, desc.pitchSemitoneJitter.min, desc.pitchSemitoneJitter.max) + pitchSemitoneOffset,
                         -24.0f, 24.0f);
    float ratio  = SemitonesToRatio(semis);
    ratio = Clamp(ratio, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO);
    sv->SetFrequencyRatio(ratio);

    // Start with fade-in if requested
    float startVol = (desc.defaultFadeInSec > 0.0f || desc.startDelaySec > 0.0f) ? 0.0f : 1.0f;
    float targetVol = 1.0f;

    // Create instance record
    VoiceInstance inst{};
    inst.id = m_nextId++;
    inst.voice = sv;
    inst.clip = clip;
    inst.eventName = eventName;
    inst.baseVolume = desc.baseVolume;
    inst.volumeScale = volumeScale;
    inst.currentVol = startVol;
    inst.targetVol = targetVol;
    inst.fadeTime = desc.defaultFadeInSec + desc.startDelaySec;
    inst.fadeElapsed = 0.0f;
    inst.fadeToSilenceThenStop = false;
    inst.looping = desc.loop;
    inst.bus = desc.bus;

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
    return &m_voicesById.find(inst.id)->second;
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

void XAudioEngine::SetBusVolume(AudioBus bus, float volume) {
    volume = Clamp(volume, 0.0f, 4.0f);
    if (bus == AudioBus::Master) {
        SetMasterVolume(volume);
        return;
    }
    auto* v = BusToSubmix(bus);
    if (v) v->SetVolume(volume);
}

float XAudioEngine::GetBusVolume(AudioBus bus) const {
    if (bus == AudioBus::Master) return m_masterVol;
    // XAudio2 doesn't expose GetVolume; we cache only master.
    // If you need queried values per submix, mirror them in your own state.
    return 1.0f;
}

void XAudioEngine::SetMasterVolume(float volume) {
    m_masterVol = Clamp(volume, 0.0f, 4.0f);
    if (m_master) m_master->SetVolume(m_masterVol);
}

float XAudioEngine::GetMasterVolume() const {
    return m_masterVol;
}

IXAudio2SubmixVoice* XAudioEngine::BusToSubmix(AudioBus bus) const {
    if (bus == AudioBus::Master) return nullptr; // source voices cannot output directly to "Master" via our routing here
    return m_submix[(int)bus];
}

// ---------------- Ambience ----------------

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

    // Start new ambience (must be looping event)
    auto evIt = m_events.find(eventName);
    if (evIt == m_events.end()) return;

    AudioEventDesc desc = evIt->second;
    desc.bus = AudioBus::Ambience;
    desc.loop = true;
    desc.defaultFadeInSec = std::max(0.0f, crossfadeSec);
    auto* newInst = PlayInternal(desc, eventName, 1.0f, 0.0f);
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

// ---------------- WAV Loader ----------------

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
        // Copy WAVEFORMATEX into the beginning of fmtExt (same first fields)
        std::memcpy(&out.fmtExt, fmtBuf.data(), sizeof(WAVEFORMATEX));
    }

    out.samples = std::move(dataBuf);
    return true;
}

// ============================ Public API Summary ============================

void XAudioEngine::RegisterAmbience(Biome biome, Climate climate, const std::string& eventName); // defined above

} // namespace colony::audio
