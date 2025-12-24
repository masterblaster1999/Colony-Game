// --------------------------- Internals ---------------------------

XAudioEngine::VoiceInstance* XAudioEngine::PlayInternal(const AudioEventDesc& desc,
                                                        const std::string& eventName,
                                                        float volumeScale,
                                                        float pitchSemitoneOffset,
                                                        const Emitter3D* emitterOpt) {
    if (!m_master) return nullptr;

    // Enforce polyphony/steal policy
    uint32_t stolenId = 0;
    if (!EnforcePolyphony(eventName, desc, stolenId) && stolenId == 0) {
        // If None policy and max reached, refuse
        auto range = m_eventToVoiceIds.equal_range(eventName);
        size_t count = std::distance(range.first, range.second);
        if (count >= desc.maxPolyphony && desc.steal == VoiceStealPolicy::None) return nullptr;
    }

    // Choose a clip (weighted if choices present)
    std::string clipId;
    RangeF volJ = desc.volumeJitter;
    RangeF pitJ = desc.pitchSemitoneJitter;
    float startOffsetSec = 0.0f;
    if (!desc.choices.empty()) {
        float totalW = 0.0f;
        for (auto& c : desc.choices) totalW += std::max(0.0f, c.weight);
        if (totalW <= 0.0f) return nullptr;
        float r = RandRange(m_rng, 0.0f, totalW);
        float acc = 0.0f;
        for (auto& c : desc.choices) {
            acc += std::max(0.0f, c.weight);
            if (r <= acc) {
                clipId = c.clipId;
                volJ   = c.volumeJitter;
                pitJ   = c.pitchSemitoneJitter;
                startOffsetSec = std::max(0.0f, c.startOffsetSec);
                break;
            }
        }
    } else {
        if (desc.clipIds.empty()) return nullptr;
        if (desc.clipIds.size() == 1) clipId = desc.clipIds.front();
        else {
            size_t idx = static_cast<size_t>(RandRange(m_rng, 0.0f, float(desc.clipIds.size() - 1e-3f)));
            clipId = desc.clipIds[idx];
        }
    }

    auto clipIt = m_clips.find(clipId);
    if (clipIt == m_clips.end()) return nullptr;
    ClipPtr clip = clipIt->second;
    const WAVEFORMATEX* wfx = clip->Wfx();

    // Sends: route to the selected bus
    IXAudio2SubmixVoice* busVoice = BusToSubmix(desc.bus);
    if (!busVoice) busVoice = BusToSubmix(AudioBus::SFX);

    XAUDIO2_SEND_DESCRIPTOR sendDesc{};
    sendDesc.Flags = 0;
    sendDesc.pOutputVoice = busVoice;

    XAUDIO2_VOICE_SENDS sends{};
    sends.SendCount = 1;
    sends.pSends = &sendDesc;

    IXAudio2SourceVoice* sv = nullptr;
    // Enable per-voice filters with USEFILTER (required for SetFilterParameters). :contentReference[oaicite:24]{index=24}
    HRESULT hr = m_xaudio->CreateSourceVoice(&sv, wfx, XAUDIO2_VOICE_USEFILTER, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends, nullptr);
    if (FAILED(hr) || !sv) return nullptr;

    // Build buffer (single submission; loop if needed)
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = static_cast<UINT32>(clip->samples.size());
    buf.pAudioData = clip->samples.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (desc.loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
        buf.LoopBegin = 0;
        buf.LoopLength = 0;
    }
    if (startOffsetSec > 0.0f && clip->sampleBytesPerFrame > 0 && wfx->nSamplesPerSec > 0) {
        const uint32_t framesPerSec = wfx->nSamplesPerSec;
        const uint32_t framesOffset = static_cast<uint32_t>(std::min<uint64_t>(
            uint64_t(startOffsetSec * framesPerSec),
            uint64_t(clip->samples.size() / clip->sampleBytesPerFrame)));
        buf.PlayBegin  = framesOffset;
        buf.PlayLength = 0; // play to end (or loop)
    }

    hr = sv->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        sv->DestroyVoice();
        return nullptr;
    }

    // Randomize volume & pitch
    float volMul = Clamp(RandRange(m_rng, volJ.min, volJ.max), 0.0f, 16.0f);
    float semis  = Clamp(RandRange(m_rng, pitJ.min, pitJ.max) + pitchSemitoneOffset, -48.0f, 48.0f);
    float ratio  = SemitonesToRatio(semis);
    ratio = std::clamp(ratio, kMinFreqRatio, kMaxFreqRatio); // CreateSourceVoice MaxFrequencyRatio / SetFrequencyRatio. :contentReference[oaicite:25]{index=25}
    sv->SetFrequencyRatio(ratio);

    // Allocate instance id before start
    uint32_t id = m_nextId++;

    // Create instance record
    VoiceInstance inst{};
    inst.id = id;
    inst.voice = sv;
    inst.clip = clip;
    inst.eventName = eventName;
    inst.baseVolume = desc.baseVolume * volMul;
    inst.volumeScale = volumeScale;
    inst.currentVol = (desc.fades.inSec > 0.0f || desc.startDelaySec > 0.0f) ? 0.0f : 1.0f;
    inst.targetVol = 1.0f;
    inst.fadeTime = desc.fades.inSec + desc.startDelaySec; // simple delay+fade behavior
    inst.fadeElapsed = 0.0f;
    inst.fadeToSilenceThenStop = false;
    inst.looping = desc.loop;
    inst.bus = desc.bus;
    inst.is3D = (emitterOpt != nullptr);
    if (inst.is3D) {
        inst.emitter = *emitterOpt;
        inst.distanceModel = desc.distanceModel;
    }

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

#if CG_AUDIO_HAS_X3DAUDIO
    if (inst.is3D) Apply3DToVoice(m_voicesById.find(inst.id)->second);
#endif
    ApplyPan(m_voicesById.find(inst.id)->second);

    return &m_voicesById.find(inst.id)->second;
}

void XAudioEngine::DestroyVoice(VoiceInstance& inst) {
    if (inst.voice) {
        inst.voice->Stop(0);
        inst.voice->FlushSourceBuffers();
        inst.voice->DestroyVoice();
        inst.voice = nullptr;
    }
}

void XAudioEngine::TickVoice(VoiceInstance& inst, float dt) {
    if (!inst.voice) return;

    // Fades (linear across fadeTime)
    if (inst.fadeTime > 0.0f) {
        inst.fadeElapsed += dt;
        float t = Clamp(inst.fadeElapsed / inst.fadeTime, 0.0f, 1.0f);
        float goal = inst.fadeToSilenceThenStop ? 0.0f : inst.targetVol;
        inst.currentVol = Lerp(inst.currentVol, goal, t);
        if (inst.fadeElapsed >= inst.fadeTime) {
            inst.fadeTime = 0.0f;
            inst.fadeElapsed = 0.0f;

            if (inst.fadeToSilenceThenStop) {
                DestroyVoice(inst);
                return;
            }
        }
    }

#if CG_AUDIO_HAS_X3DAUDIO
    if (inst.is3D) {
        Apply3DToVoice(inst); // update doppler + matrix as emitter/listener moves
    }
#endif

    // Apply occlusion as a simple LPF + gain scaling
    ApplyOcclusion(inst);

    // Pan (2D)
    ApplyPan(inst);

    // Apply composite gain
    if (inst.voice) {
        inst.voice->SetVolume(inst.currentVol * inst.baseVolume * inst.volumeScale);
    }
}

void XAudioEngine::ApplyFade(VoiceInstance& /*inst*/, float /*dt*/) {
    // handled inline in TickVoice (kept for API symmetry)
}

void XAudioEngine::ApplyPan(VoiceInstance& inst) {
    if (!inst.voice) return;
    IXAudio2SubmixVoice* bus = BusToSubmix(inst.bus);
    if (!bus) return;

    XAUDIO2_VOICE_DETAILS sd{}, dd{};
    inst.voice->GetVoiceDetails(&sd);
    bus->GetVoiceDetails(&dd);

    if (dd.InputChannels < 2) return; // nothing to pan to

    std::vector<float> mat;
    BuildStereoPanMatrix(inst.pan, sd.InputChannels, dd.InputChannels, mat);
    if (!mat.empty()) {
        inst.voice->SetOutputMatrix(bus, sd.InputChannels, dd.InputChannels, mat.data()); // SetOutputMatrix. :contentReference[oaicite:26]{index=26}
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
            DestroyVoice(inst);
            dead.push_back(kv.first);
        }
    }

    for (uint32_t id : dead) {
        auto it = m_voicesById.find(id);
        if (it != m_voicesById.end()) {
            // callback before unlink
            if (m_onEventEnd) {
                AudioEventHandle h{it->second.id};
                m_onEventEnd(h, it->second.eventName);
            }
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

IXAudio2SubmixVoice* XAudioEngine::BusToSubmix(AudioBus bus) const {
    if (bus == AudioBus::Master) return nullptr; // source voices don't output directly to Master
    return m_submix[(int)bus];
}

