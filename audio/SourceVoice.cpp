#include "SourceVoice.h"
#include <xaudio2.h>
#include <stdexcept>

SourceVoice::~SourceVoice(){
    Stop();
    if (m_voice){ m_voice->DestroyVoice(); m_voice = nullptr; }
}

void SourceVoice::LoadFromFile(const std::wstring& path)
{
    auto [fmt, bytes] = wav::LoadWholeFile(path);
    m_fmt  = std::move(fmt);
    m_data = std::move(bytes);

    if (m_voice){ m_voice->DestroyVoice(); m_voice = nullptr; }

    if (FAILED(m_engine.XAudio()->CreateSourceVoice(&m_voice, m_fmt.get())))
        throw std::runtime_error("CreateSourceVoice failed");
}

void SourceVoice::Play(bool loop)
{
    if (!m_voice || m_data.empty()) return;

    XAUDIO2_BUFFER buf{}; 
    buf.AudioBytes = static_cast<UINT32>(m_data.size());
    buf.pAudioData = m_data.data();
    if (loop){
        buf.LoopBegin  = 0;
        buf.LoopLength = 0;                  // Loop full buffer
        buf.LoopCount  = XAUDIO2_LOOP_INFINITE;
    } else {
        buf.Flags = XAUDIO2_END_OF_STREAM;   // mark end for callbacks
    }

    if (FAILED(m_voice->FlushSourceBuffers())) {/* ignore */}
    if (FAILED(m_voice->SubmitSourceBuffer(&buf)))
        throw std::runtime_error("SubmitSourceBuffer failed");
    if (FAILED(m_voice->Start(0)))
        throw std::runtime_error("SourceVoice Start failed");
}

void SourceVoice::Stop()
{
    if (!m_voice) return;
    m_voice->Stop(0);
    m_voice->FlushSourceBuffers();
}

void SourceVoice::SetVolume(float v)
{
    if (m_voice) m_voice->SetVolume(v);
}
