#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <atomic>

//
// XAudio2 engine (Windows-only)
// - Minimal, robust wrapper for in-game audio
// - One-shot and looping playback
// - Optional WAV loader (PCM / IEEE float)
// - No external dependencies beyond XAudio2
//
// References:
//   - XAudio2 initialization & mastering voice creation. (Microsoft Learn) 
//   - Playing a sound with IXAudio2SourceVoice + XAUDIO2_BUFFER. (Microsoft Learn) 
//

namespace audio {

using Microsoft::WRL::ComPtr;

// -------------------------------------------------------------------------------------------------
// AudioClip: raw PCM or IEEE float samples + WAVEFORMAT(EXTENSIBLE) blob
// The format blob can be WAVEFORMATEX or WAVEFORMATEXTENSIBLE; pass wf() to XAudio2 APIs.
// -------------------------------------------------------------------------------------------------
struct AudioClip {
    std::vector<std::uint8_t> format; // bytes of WAVEFORMATEX or WAVEFORMATEXTENSIBLE
    std::vector<std::uint8_t> samples; // PCM/float frames (interleaved for nChannels > 1)

    const WAVEFORMATEX* wf() const noexcept {
        return reinterpret_cast<const WAVEFORMATEX*>(format.data());
    }
    bool empty() const noexcept { return samples.empty() || format.empty(); }

    // Convenience: sample count (frames * channels) using nBlockAlign
    std::uint32_t total_sample_frames() const noexcept {
        if (format.size() < sizeof(WAVEFORMATEX)) return 0;
        const auto* w = wf();
        if (!w || w->nBlockAlign == 0) return 0;
        return static_cast<std::uint32_t>(samples.size() / w->nBlockAlign);
    }
};

// -------------------------------------------------------------------------------------------------
// Voice: RAII wrapper for an IXAudio2SourceVoice
// - Owns (or references) the submitted sample data for the life of playback
// - Supports one-shot or looping
// - Volume / pitch control
// - WaitUntilFinished() helper for fire-and-forget sounds (optional)
// -------------------------------------------------------------------------------------------------
class Voice {
public:
    // Create a source voice from 'clip' format; if 'copySamples' is true, the voice
    // holds a private copy of sample bytes so the caller can discard 'clip.samples'.
    // If false, the caller must ensure 'clip.samples' outlives playback.
    static std::unique_ptr<Voice> Create(IXAudio2* xa, const AudioClip& clip, bool copySamples = true);

    ~Voice();

    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    // Submit & start playback. If loop==true, the whole clip loops (LoopBegin=0..LoopLength=total).
    bool Play(bool loop, float volume = 1.0f, float pitchRatio = 1.0f /* 0.5..2.0 typical */);

    // Stop consumption immediately (optionally with XAUDIO2_PLAY_TAILS semantics if you prefer).
    void Stop(bool playTails = false);

    // Volume [0..>1], Pitch (frequency ratio)
    void SetVolume(float v);
    void SetPitch(float ratio);

    // Returns true while voice has buffers queued/playing.
    bool IsPlaying() const;

    // Block until the submitted buffer finishes (no-op if looping).
    // Returns false on timeout/error, true if finished.
    bool WaitUntilFinished(DWORD timeout_ms = INFINITE);

    IXAudio2SourceVoice* raw() const noexcept { return m_voice; }

private:
    Voice() = default;
    bool init_(IXAudio2* xa, const AudioClip& clip, bool copySamples);
    void destroy_();

    // Internal callback implementation signals end-of-buffer
    struct Callback;
    Callback*                 m_cb     = nullptr;
    IXAudio2SourceVoice*      m_voice  = nullptr;

    // We keep a copy of the clip's format bytes.
    std::vector<std::uint8_t> m_fmt;      // WAVEFORMAT(EX)
    // Either points to external samples (view) or owns a copy:
    const std::uint8_t*       m_audioPtr = nullptr;
    std::vector<std::uint8_t> m_owned;    // when copySamples==true
    XAUDIO2_BUFFER            m_xbuf{};   // submitted buffer

    std::atomic<bool>         m_started{false};
    std::atomic<bool>         m_looping{false};
};

// -------------------------------------------------------------------------------------------------
// XAudio2Engine: create/destroy the engine & mastering voice; helper to play one-shots
// -------------------------------------------------------------------------------------------------
class XAudio2Engine {
public:
    // requestDebug: adds XAUDIO2_DEBUG_ENGINE flag on XAudio2Create in debug builds.
    bool Initialize(bool requestDebug = false);
    void Shutdown();

    bool IsInitialized() const noexcept { return !!m_xaudio; }

    // Set master volume [0..1+]
    void SetMasterVolume(float v);

    // Quick one-shot (returns nullptr on failure). If loop==true, playback loops.
    // If you pass copySamples=false, 'clip.samples' must stay valid until playback stops.
    std::unique_ptr<Voice> PlayOneShot(const AudioClip& clip, bool loop = false,
                                       float volume = 1.0f, float pitchRatio = 1.0f,
                                       bool copySamples = true);

    IXAudio2*                 xa()  const noexcept { return m_xaudio.Get(); }
    IXAudio2MasteringVoice*   mix() const noexcept { return m_master; }

private:
    ComPtr<IXAudio2>          m_xaudio;
    IXAudio2MasteringVoice*   m_master = nullptr;
};

// -------------------------------------------------------------------------------------------------
// Optional helpers: Load a simple RIFF/WAVE (PCM or IEEE float) from disk into an AudioClip.
// - Supports WAVEFORMATEX and WAVEFORMATEXTENSIBLE "fmt " chunks
// - Does not perform format conversion; you should author content as PCM16 or float32
// -------------------------------------------------------------------------------------------------
bool LoadWavFile(const std::wstring& path, AudioClip& outClip, std::wstring* error = nullptr);

} // namespace audio
