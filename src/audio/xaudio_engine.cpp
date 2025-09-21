#include "xaudio_engine.h"
#include <mmreg.h>          // WAVE_FORMAT_PCM, WAVE_FORMAT_IEEE_FLOAT
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>

// Link to in-box XAudio2 (Windows 10/11). If you use the XAudio2.9 Redistributable NuGet,
// link against xaudio2_9redist.lib instead in your CMake.
// CMake: target_link_libraries(ColonyGame PRIVATE xaudio2)
#pragma comment(lib, "xaudio2.lib")

using namespace audio;

//==================================================================================================
// Internal helpers
//==================================================================================================
namespace {

inline void dbgprintA(const char* s) {
#if defined(_DEBUG)
    OutputDebugStringA(s);
#else
    (void)s;
#endif
}

struct VoiceEvent {
    HANDLE h = nullptr;
    VoiceEvent()  { h = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~VoiceEvent() { if (h) CloseHandle(h); }
    void Reset()  { if (h) ResetEvent(h); }
    void Signal() { if (h) SetEvent(h); }
    bool Wait(DWORD ms = INFINITE) {
        if (!h) return true;
        return WaitForSingleObjectEx(h, ms, FALSE) == WAIT_OBJECT_0;
    }
};

} // anon

//==================================================================================================
// Voice::Callback (IXAudio2VoiceCallback impl)
//==================================================================================================
struct Voice::Callback : IXAudio2VoiceCallback {
    VoiceEvent bufferEnd;
    std::atomic<bool> streamEnded{false};

    // No-op methods we do not use:
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) noexcept override {}
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override { streamEnded.store(true, std::memory_order_release); }
    void STDMETHODCALLTYPE OnBufferEnd(void*) noexcept override { bufferEnd.Signal(); }
};

//==================================================================================================
// Voice
//==================================================================================================
std::unique_ptr<Voice> Voice::Create(IXAudio2* xa, const AudioClip& clip, bool copySamples) {
    auto v = std::unique_ptr<Voice>(new Voice());
    if (!v->init_(xa, clip, copySamples)) {
        v.reset();
    }
    return v;
}

bool Voice::init_(IXAudio2* xa, const AudioClip& clip, bool copySamples) {
    if (!xa || clip.empty()) return false;

    // Copy format blob from clip
    m_fmt = clip.format;
    const auto* wf = reinterpret_cast<const WAVEFORMATEX*>(m_fmt.data());

    // Prepare callback and source voice
    m_cb = new Callback();
    HRESULT hr = xa->CreateSourceVoice(&m_voice, wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO, m_cb);
    if (FAILED(hr)) {
        dbgprintA("[Audio] CreateSourceVoice failed.\n");
        delete m_cb; m_cb = nullptr;
        return false;
    }

    // Own samples or reference caller data
    if (copySamples) {
        m_owned = clip.samples;
        m_audioPtr = m_owned.data();
    } else {
        m_audioPtr = clip.samples.data();
    }

    // Fill a single XAUDIO2_BUFFER. Looping (if any) is set in Play().
    ZeroMemory(&m_xbuf, sizeof(m_xbuf));
    m_xbuf.pAudioData = m_audioPtr;
    m_xbuf.AudioBytes = static_cast<UINT32>(clip.samples.size());
    m_xbuf.Flags      = XAUDIO2_END_OF_STREAM; // single-buffer submission

    return true;
}

Voice::~Voice() {
    destroy_();
}

void Voice::destroy_() {
    if (m_voice) {
        // Stop and flush any outstanding buffers.
        m_voice->Stop(0);
        m_voice->FlushSourceBuffers();
        m_voice->DestroyVoice();
        m_voice = nullptr;
    }
    if (m_cb) {
        delete m_cb;
        m_cb = nullptr;
    }
    m_owned.clear();
    m_fmt.clear();
    m_audioPtr = nullptr;
    m_started.store(false, std::memory_order_release);
}

bool Voice::Play(bool loop, float volume, float pitchRatio) {
    if (!m_voice) return false;

    // Program looping (whole clip) if requested. Loop counts: XAUDIO2_LOOP_INFINITE or N times.
    m_looping.store(loop, std::memory_order_release);

    if (loop) {
        const auto* wf = reinterpret_cast<const WAVEFORMATEX*>(m_fmt.data());
        if (!wf || wf->nBlockAlign == 0) return false;
        const UINT32 totalFrames = static_cast<UINT32>(m_owned.empty()
                                     ? (m_xbuf.AudioBytes / wf->nBlockAlign)
                                     : (m_owned.size()     / wf->nBlockAlign));
        m_xbuf.LoopBegin  = 0;
        m_xbuf.LoopLength = totalFrames;
        m_xbuf.LoopCount  = XAUDIO2_LOOP_INFINITE;
    } else {
        m_xbuf.LoopBegin  = 0;
        m_xbuf.LoopLength = 0;
        m_xbuf.LoopCount  = 0;
    }

    // (Re)submit buffer & start
    m_cb->bufferEnd.Reset();
    HRESULT hr = m_voice->FlushSourceBuffers();
    (void)hr; // harmless if empty
    hr = m_voice->SubmitSourceBuffer(&m_xbuf);
    if (FAILED(hr)) {
        dbgprintA("[Audio] SubmitSourceBuffer failed.\n");
        return false;
    }
    SetVolume(volume);
    SetPitch(pitchRatio);

    hr = m_voice->Start(0 /*No flags*/);
    if (FAILED(hr)) {
        dbgprintA("[Audio] IXAudio2SourceVoice::Start failed.\n");
        return false;
    }

    m_started.store(true, std::memory_order_release);
    return true;
}

void Voice::Stop(bool playTails) {
    if (!m_voice) return;
    m_voice->Stop(playTails ? XAUDIO2_PLAY_TAILS : 0);
    if (!playTails) {
        m_voice->FlushSourceBuffers();
    }
    m_started.store(false, std::memory_order_release);
}

void Voice::SetVolume(float v) {
    if (!m_voice) return;
    // XAudio2 supports > 1.0f; clamp to a sane range
    const float clamped = (v < 0.0f) ? 0.0f : v;
    m_voice->SetVolume(clamped);
}

void Voice::SetPitch(float ratio) {
    if (!m_voice) return;
    // Frequency ratio typical range is [0.5 .. 2.0]
    const float r = (ratio <= 0.f) ? 1.0f : ratio;
    m_voice->SetFrequencyRatio(r);
}

bool Voice::IsPlaying() const {
    if (!m_voice) return false;
    XAUDIO2_VOICE_STATE st{};
    // Passing XAUDIO2_VOICE_NOSAMPLESPLAYED is faster when you only need buffer queue state.
    m_voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return st.BuffersQueued > 0 || m_started.load(std::memory_order_acquire);
}

bool Voice::WaitUntilFinished(DWORD timeout_ms) {
    if (!m_voice || m_looping.load(std::memory_order_acquire)) return true;
    return m_cb->bufferEnd.Wait(timeout_ms);
}

//==================================================================================================
// XAudio2Engine
//==================================================================================================
bool XAudio2Engine::Initialize(bool requestDebug) {
    Shutdown(); // in case of re-init

    UINT32 flags = 0;
#if defined(_DEBUG)
    if (requestDebug) flags |= XAUDIO2_DEBUG_ENGINE;
#endif

    // Create engine
    HRESULT hr = XAudio2Create(m_xaudio.ReleaseAndGetAddressOf(), flags, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        dbgprintA("[Audio] XAudio2Create failed.\n");
        return false;
    }

    // Create default mastering voice (system default device, auto channel/rate)
    hr = m_xaudio->CreateMasteringVoice(&m_master);
    if (FAILED(hr)) {
        dbgprintA("[Audio] CreateMasteringVoice failed.\n");
        m_xaudio.Reset();
        return false;
    }

    return true;
}

void XAudio2Engine::Shutdown() {
    if (m_master) {
        m_master->DestroyVoice();
        m_master = nullptr;
    }
    if (m_xaudio) {
        // XAudio2 engine cleans up internal threads/objects on release.
        m_xaudio.Reset();
    }
}

void XAudio2Engine::SetMasterVolume(float v) {
    if (!m_master) return;
    const float clamped = (v < 0.0f) ? 0.0f : v;
    m_master->SetVolume(clamped);
}

std::unique_ptr<Voice> XAudio2Engine::PlayOneShot(const AudioClip& clip, bool loop,
                                                  float volume, float pitchRatio,
                                                  bool copySamples) {
    if (!m_xaudio || !m_master || clip.empty()) return nullptr;

    auto v = Voice::Create(m_xaudio.Get(), clip, copySamples);
    if (!v) return nullptr;

    if (!v->Play(loop, volume, pitchRatio)) {
        v.reset();
        return nullptr;
    }
    return v;
}

//==================================================================================================
// WAV loader (PCM / IEEE float)
//  - Accepts 'fmt ' chunk with WAVEFORMATEX or WAVEFORMATEXTENSIBLE
//  - Copies bytes into AudioClip::format and ::samples without conversion
//==================================================================================================
namespace {

struct RiffHeader { // 12 bytes
    char riff[4];      // "RIFF"
    uint32_t size;     // overall size minus 8
    char wave[4];      // "WAVE"
};
struct ChunkHeader {
    char id[4];        // e.g., "fmt ", "data", "fact", "smpl", ...
    uint32_t size;     // bytes following the header
};

static bool read_exact(std::ifstream& f, void* dst, std::size_t cb) {
    return !!f.read(reinterpret_cast<char*>(dst), std::streamsize(cb));
}
static void skip_exact(std::ifstream& f, std::size_t cb) {
    f.seekg(std::streamoff(cb), std::ios::cur);
}
static bool equal4(const char id[4], const char* lit) {
    return std::memcmp(id, lit, 4) == 0;
}

} // anon

bool audio::LoadWavFile(const std::wstring& path, AudioClip& outClip, std::wstring* error) {
    outClip = AudioClip{};
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = L"Failed to open WAV file.";
        return false;
    }

    RiffHeader rh{};
    if (!read_exact(f, &rh, sizeof(rh))) {
        if (error) *error = L"File too small for RIFF header.";
        return false;
    }
    if (!equal4(rh.riff, "RIFF") || !equal4(rh.wave, "WAVE")) {
        if (error) *error = L"Not a RIFF/WAVE file.";
        return false;
    }

    std::vector<std::uint8_t> fmtChunk;
    std::vector<std::uint8_t> dataChunk;

    // Iterate chunks
    while (f && !f.eof()) {
        ChunkHeader ch{};
        if (!read_exact(f, &ch, sizeof(ch))) break;

        if (equal4(ch.id, "fmt ")) {
            fmtChunk.resize(ch.size);
            if (!read_exact(f, fmtChunk.data(), ch.size)) {
                if (error) *error = L"Truncated 'fmt ' chunk.";
                return false;
            }
        } else if (equal4(ch.id, "data")) {
            dataChunk.resize(ch.size);
            if (!read_exact(f, dataChunk.data(), ch.size)) {
                if (error) *error = L"Truncated 'data' chunk.";
                return false;
            }
        } else {
            // Skip other chunks (fact, smpl, LIST, etc.)
            skip_exact(f, ch.size);
        }

        // Chunks are word-aligned; odd sizes are padded with one byte
        if (ch.size & 1) skip_exact(f, 1);
    }

    if (fmtChunk.empty() || dataChunk.empty()) {
        if (error) *error = L"Missing 'fmt ' or 'data' chunk.";
        return false;
    }

    // Basic sanity: format blob should be at least WAVEFORMATEX
    if (fmtChunk.size() < sizeof(WAVEFORMATEX)) {
        if (error) *error = L"'fmt ' chunk too small.";
        return false;
    }

    // No conversion; XAudio2 accepts either WAVEFORMATEX or WAVEFORMATEXTENSIBLE as-is
    const auto* wf = reinterpret_cast<const WAVEFORMATEX*>(fmtChunk.data());
    if (wf->nChannels == 0 || wf->nBlockAlign == 0 || wf->nSamplesPerSec == 0) {
        if (error) *error = L"Invalid WAVEFORMAT data.";
        return false;
    }

    outClip.format  = std::move(fmtChunk);
    outClip.samples = std::move(dataChunk);
    return true;
}
