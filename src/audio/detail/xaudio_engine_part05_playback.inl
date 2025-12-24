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

