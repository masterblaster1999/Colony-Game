#include "MusicStream.h"
#include <stdexcept>
#include <cstdio>

using namespace std;

MusicStream::MusicStream(AudioEngine& engine) : m_engine(engine) {}
MusicStream::~MusicStream(){ Stop(); if (m_voice){ m_voice->DestroyVoice(); m_voice=nullptr; } }

void MusicStream::Open(const std::wstring& path, bool loop, size_t bufferMillis)
{
    Stop(); // ensure any previous stream is down

    auto info = wav::ReadHeader(path);
    m_fmt.reset(info.wfx, [](void* p){ delete[] reinterpret_cast<uint8_t*>(p); });
    m_dataOffset = info.dataOffset;
    m_dataBytes  = info.dataBytes;
    m_path       = path;
    m_loop       = loop;

    if (m_voice){ m_voice->DestroyVoice(); m_voice = nullptr; }
    if (FAILED(m_engine.XAudio()->CreateSourceVoice(&m_voice, m_fmt.get(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_cb)))
        throw std::runtime_error("CreateSourceVoice (music) failed");

    // Compute buffer size in bytes ≈ bufferMillis of audio, align to frames
    const uint32_t bpf = wav::BytesPerFrame(*m_fmt);
    const uint32_t bytesPerSec = m_fmt->nSamplesPerSec * bpf;
    size_t raw = (bytesPerSec * bufferMillis) / 1000;
    m_bytesPerBuffer = (raw / bpf) * bpf; // frame aligned
    m_bytesPerBuffer = max<size_t>(m_bytesPerBuffer, bpf * 256); // floor

    m_bufA.resize(m_bytesPerBuffer);
    m_bufB.resize(m_bytesPerBuffer);
}

void MusicStream::Play()
{
    if (!m_voice) return;
    if (m_running.load()) return;
    m_running = true;
    m_thread  = std::thread(&MusicStream::StreamingThread, this);
}

void MusicStream::Stop()
{
    if (!m_running.exchange(false)) { // if not running, nothing to do
        if (m_voice){ m_voice->Stop(0); m_voice->FlushSourceBuffers(); }
        return;
    }
    if (m_voice){ m_voice->Stop(0); m_voice->FlushSourceBuffers(); }
    { // wake any waiter
        std::lock_guard<std::mutex> lk(m_cb.mtx);
        m_cb.bufferEnded = true;
        m_cb.cv.notify_all();
    }
    if (m_thread.joinable()) m_thread.join();
}

void MusicStream::SetVolume(float v)
{
    if (m_voice) m_voice->SetVolume(v);
}

void MusicStream::StreamingThread()
{
    FILE* f=nullptr;
    if(_wfopen_s(&f, m_path.c_str(), L"rb") || !f) { m_running=false; return; }
    auto closeF = std::unique_ptr<FILE, int(*)(FILE*)>(f, fclose);
    _fseeki64(f, m_dataOffset, SEEK_SET);

    auto readChunk = [&](std::vector<uint8_t>& dst)->size_t{
        return fread(dst.data(), 1, dst.size(), f);
    };

    auto submit = [&](std::vector<uint8_t>& buf, size_t valid, bool eos){
        XAUDIO2_BUFFER xb{}; xb.AudioBytes = (UINT32)valid;
        xb.pAudioData = buf.data();
        if (eos) xb.Flags = XAUDIO2_END_OF_STREAM;
        if (FAILED(m_voice->SubmitSourceBuffer(&xb))) return false;
        return true;
    };

    // Prime the queue with two buffers
    size_t readA = readChunk(m_bufA);
    size_t readB = readChunk(m_bufB);
    bool atEOF   = (readB == 0);
    if (!submit(m_bufA, readA, atEOF && !m_loop)) { m_running=false; return; }
    if (readB) { if (!submit(m_bufB, readB, !m_loop && _ftelli64(f) >= (m_dataOffset + (long long)m_dataBytes))) { m_running=false; return; } }

    if (FAILED(m_voice->Start(0))) { m_running=false; return; }

    bool flip=false;
    uint64_t played = (uint64_t)readA + (uint64_t)readB;

    while (m_running.load())
    {
        // Wait until XAUDIO2 tells us a buffer finished
        unique_lock<std::mutex> lk(m_cb.mtx);
        m_cb.cv.wait(lk, [&]{ return !m_running.load() || m_cb.bufferEnded; });
        m_cb.bufferEnded = false;
        lk.unlock();
        if (!m_running.load()) break;

        // Fill next buffer
        std::vector<uint8_t>& tgt = flip ? m_bufA : m_bufB;
        size_t got = readChunk(tgt);
        bool eos = false;

        if (got == 0) {
            if (m_loop) {
                _fseeki64(f, m_dataOffset, SEEK_SET);
                got  = readChunk(tgt);
                eos  = false;
                played = 0;
            } else {
                eos = true;
            }
        } else {
            played += got;
            uint64_t filePos = _ftelli64(f);
            eos = (!m_loop && filePos >= (m_dataOffset + (long long)m_dataBytes));
        }

        if (!submit(tgt, got, eos)) break;
        flip = !flip;

        if (eos) break; // let OnStreamEnd wake us and exit
    }
}
