#include "AudioEngine.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

void AudioEngine::Initialize()
{
    if (!m_comInit) {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            throw std::runtime_error("CoInitializeEx failed");
        m_comInit = true;
    }

    UINT32 flags = 0;
#ifdef _DEBUG
    flags |= XAUDIO2_DEBUG_ENGINE;
#endif

    IXAudio2* xa = nullptr;
    if (FAILED(::XAudio2Create(&xa, flags, XAUDIO2_DEFAULT_PROCESSOR)))
        throw std::runtime_error("XAudio2Create failed");

    IXAudio2MasteringVoice* mv = nullptr;
    if (FAILED(xa->CreateMasteringVoice(&mv, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE)))
    {
        xa->Release();
        throw std::runtime_error("CreateMasteringVoice failed");
    }

    m_xaudio = xa;
    m_master = mv;
}

void AudioEngine::Shutdown()
{
    if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
    if (m_xaudio) { m_xaudio->Release(); m_xaudio = nullptr; }
    if (m_comInit){ ::CoUninitialize(); m_comInit = false; }
}

AudioEngine::~AudioEngine() { Shutdown(); }

void AudioEngine::SetMasterVolume(float v)
{
    if (m_master) m_master->SetVolume(v);
}
