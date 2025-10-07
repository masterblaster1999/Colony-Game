#pragma once
#include <xaudio2.h>
#include <windows.h>
#include <stdexcept>

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();

    void Initialize();           // CoInit + XAudio2 + mastering voice
    void Shutdown();

    IXAudio2*                 XAudio() const { return m_xaudio; }
    IXAudio2MasteringVoice*   Master() const { return m_master; }

    void SetMasterVolume(float v);

private:
    bool     m_comInit = false;
    IXAudio2* m_xaudio = nullptr;
    IXAudio2MasteringVoice* m_master = nullptr;
};
