#pragma once
#include "AudioEngine.h"
#include "Wav.h"
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>

class MusicStream {
public:
    explicit MusicStream(AudioEngine& engine);
    ~MusicStream();

    // Open a WAV for streaming; creates its own source voice.
    void Open(const std::wstring& path, bool loop=false, size_t bufferMillis=200);

    // Start/stop streaming.
    void Play();
    void Stop();           // blocks until streaming thread exits

    void SetVolume(float v);

private:
    struct VoiceCallback : public IXAudio2VoiceCallback {
        std::mutex mtx;
        std::condition_variable cv;
        bool bufferEnded = false;
        void Signal(){ std::lock_guard<std::mutex> lk(mtx); bufferEnded = true; cv.notify_one(); }

        // IXAudio2VoiceCallback impl (only ones we use)
        void __stdcall OnBufferEnd(void*) override { Signal(); }
        void __stdcall OnStreamEnd() override { Signal(); }
        void __stdcall OnBufferStart(void*) override {}
        void __stdcall OnLoopEnd(void*) override {}
        void __stdcall OnVoiceProcessingPassStart(UINT32) override {}
        void __stdcall OnVoiceProcessingPassEnd() override {}
        void __stdcall OnVoiceError(void*, HRESULT) override {}
    };

    void StreamingThread();

    AudioEngine& m_engine;
    IXAudio2SourceVoice* m_voice = nullptr;
    VoiceCallback m_cb;

    std::wstring m_path;
    std::unique_ptr<WAVEFORMATEX[], void(*)(void*)> m_fmt{nullptr, [](void*){}};
    uint64_t m_dataOffset = 0;
    uint64_t m_dataBytes  = 0;

    size_t m_bytesPerBuffer = 0;
    bool   m_loop = false;

    std::atomic<bool> m_running{false};
    std::thread m_thread;

    // Double-buffer
    std::vector<uint8_t> m_bufA, m_bufB;
};
