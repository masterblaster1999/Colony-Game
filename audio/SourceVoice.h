#pragma once
#include "AudioEngine.h"
#include "Wav.h"
#include <memory>
#include <vector>

class SourceVoice {
public:
    explicit SourceVoice(AudioEngine& engine) : m_engine(engine) {}
    ~SourceVoice();

    // Load a short WAV fully into memory (PCM or IEEE float).
    void LoadFromFile(const std::wstring& path);

    // Play once or loop indefinitely; returns immediately.
    void Play(bool loop = false);

    // Stop playback and flush queued buffers.
    void Stop();

    void SetVolume(float v);

private:
    AudioEngine& m_engine;
    IXAudio2SourceVoice* m_voice = nullptr;

    std::unique_ptr<WAVEFORMATEX[], void(*)(void*)> m_fmt{nullptr, [](void*){}};
    std::vector<uint8_t> m_data;
};
