// --------------------------- Bus FX (Reverb/Meter/EQ/Echo/Limiter) ---------------------------

// NOTE: Avoid referencing private nested types outside the class.
// Build the effect chain using generic flags + IUnknown* pointers, not slot type names.
#if CG_AUDIO_HAS_XAUDIO2FX || CG_AUDIO_HAS_XAPOFX
static void RebuildBusEffectChain(IXAudio2SubmixVoice* v,
#if CG_AUDIO_HAS_XAPOFX
    bool eqEnabled, IUnknown* eqFx,
    bool echoEnabled, IUnknown* echoFx,
#else
    bool /*eqEnabled*/, IUnknown* /*eqFx*/,
    bool /*echoEnabled*/, IUnknown* /*echoFx*/,
#endif
#if CG_AUDIO_HAS_XAUDIO2FX
    bool reverbEnabled, IUnknown* reverbFx,
    bool meterEnabled,  IUnknown* meterFx
#else
    bool /*reverbEnabled*/, IUnknown* /*reverbFx*/,
    bool /*meterEnabled*/,  IUnknown* /*meterFx*/
#endif
) {
    if (!v) return;

    std::vector<XAUDIO2_EFFECT_DESCRIPTOR> effs;
#if CG_AUDIO_HAS_XAPOFX
    if (eqEnabled  && eqFx)   { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = eqFx;   d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
    if (echoEnabled&& echoFx) { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = echoFx; d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
#endif
#if CG_AUDIO_HAS_XAUDIO2FX
    if (reverbEnabled && reverbFx) { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = reverbFx; d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
    if (meterEnabled  && meterFx)  { XAUDIO2_EFFECT_DESCRIPTOR d{}; d.pEffect = meterFx;  d.InitialState = TRUE; d.OutputChannels = 0; effs.push_back(d); }
#endif

    if (!effs.empty()) {
        XAUDIO2_EFFECT_CHAIN chain{ static_cast<UINT32>(effs.size()), effs.data() };
        v->SetEffectChain(&chain); // How-to: Create an Effect Chain. :contentReference[oaicite:15]{index=15}
    } else {
        // Clear chain
        XAUDIO2_EFFECT_CHAIN chain{ 0, nullptr };
        v->SetEffectChain(&chain);
    }
}
#endif

#if CG_AUDIO_HAS_XAUDIO2FX
void XAudioEngine::EnableBusReverb(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;

    if (enable && !m_reverb[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(XAudio2CreateReverb(&fx))) {       // XAudio2CreateReverb. :contentReference[oaicite:16]{index=16}
            m_reverb[(int)bus].fx = fx;
        }
    }
    m_reverb[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
#if CG_AUDIO_HAS_XAPOFX
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#else
        false, nullptr, false, nullptr,
#endif
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx);
}

void XAudioEngine::SetBusReverbParams(AudioBus bus, const XAUDIO2FX_REVERB_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_reverb[(int)bus];
    if (!v || !slot.fx) return;
    // Effect index depends on chain order; rebuild guarantees index:
    // EQ(0), Echo(1), Reverb(2), Meter(3). So reverb index is (#EQ + #Echo).
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    v->SetEffectParameters(idx, &p, sizeof(p));       // Reverb params; scale if needed by sample rate per docs. :contentReference[oaicite:17]{index=17}
}

bool XAudioEngine::GetBusReverbParams(AudioBus bus, XAUDIO2FX_REVERB_PARAMETERS& out) const {
    if (bus == AudioBus::Master) return false;
    auto* v = m_submix[(int)bus];
    auto& slot = m_reverb[(int)bus];
    if (!v || !slot.fx) return false;
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    return SUCCEEDED(v->GetEffectParameters(idx, &out, sizeof(out)));
}

void XAudioEngine::EnableBusMeter(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;

    if (enable && !m_meter[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(XAudio2CreateVolumeMeter(&fx, 0))) {  // VolumeMeter. :contentReference[oaicite:18]{index=18}
            m_meter[(int)bus].fx = fx;
        }
    }
    m_meter[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
#if CG_AUDIO_HAS_XAPOFX
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#else
        false, nullptr, false, nullptr,
#endif
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx);
}

bool XAudioEngine::GetBusMeterLevels(AudioBus bus, std::vector<float>& channelPeaks) const {
    if (bus == AudioBus::Master) return false;
    auto* v = m_submix[(int)bus];
    auto& slot = m_meter[(int)bus];
    if (!v || !slot.fx || !slot.enabled) return false;

    // Meter index is last in our chain.
    UINT32 idx = 0;
#if CG_AUDIO_HAS_XAPOFX
    if (m_eq[(int)bus].enabled)   ++idx;
    if (m_echo[(int)bus].enabled) ++idx;
#endif
    if (m_reverb[(int)bus].enabled) ++idx;

    XAUDIO2_VOICE_DETAILS vd{};
    v->GetVoiceDetails(&vd);
    struct MeterLevels { float PeakLevel[XAUDIO2_MAX_AUDIO_CHANNELS]; float RMSLevel[XAUDIO2_MAX_AUDIO_CHANNELS]; };
    MeterLevels levels{};
    if (FAILED(v->GetEffectParameters(idx, &levels, sizeof(levels)))) return false;

    channelPeaks.assign(levels.PeakLevel, levels.PeakLevel + vd.InputChannels);
    return true;
}
#endif // CG_AUDIO_HAS_XAUDIO2FX

#if CG_AUDIO_HAS_XAPOFX
void XAudioEngine::EnableBusEQ(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;
    if (enable && !m_eq[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(CreateFX(__uuidof(FXEQ), &fx))) { // Create FXEQ. :contentReference[oaicite:19]{index=19}
            m_eq[(int)bus].fx = fx;
        }
    }
    m_eq[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#if CG_AUDIO_HAS_XAUDIO2FX
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx
#else
        false, nullptr, false, nullptr
#endif
    );
}

void XAudioEngine::SetBusEQParams(AudioBus bus, const FXEQ_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_eq[(int)bus];
    if (!v || !slot.fx) return;

    // EQ index at start of chain if enabled.
    UINT32 idx = 0;
    v->SetEffectParameters(idx, &p, sizeof(p));       // FXEQ params. :contentReference[oaicite:20]{index=20}
}

void XAudioEngine::EnableBusEcho(AudioBus bus, bool enable) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    if (!v) return;
    if (enable && !m_echo[(int)bus].fx) {
        IUnknown* fx = nullptr;
        if (SUCCEEDED(CreateFX(__uuidof(FXEcho), &fx))) { // Create FXEcho. :contentReference[oaicite:21]{index=21}
            m_echo[(int)bus].fx = fx;
        }
    }
    m_echo[(int)bus].enabled = enable;

    RebuildBusEffectChain(v,
        m_eq[(int)bus].enabled,   m_eq[(int)bus].fx,
        m_echo[(int)bus].enabled, m_echo[(int)bus].fx,
#if CG_AUDIO_HAS_XAUDIO2FX
        m_reverb[(int)bus].enabled, m_reverb[(int)bus].fx,
        m_meter[(int)bus].enabled,  m_meter[(int)bus].fx
#else
        false, nullptr, false, nullptr
#endif
    );
}

void XAudioEngine::SetBusEchoParams(AudioBus bus, const FXECHO_PARAMETERS& p) {
    if (bus == AudioBus::Master) return;
    auto* v = m_submix[(int)bus];
    auto& slot = m_echo[(int)bus];
    if (!v || !slot.fx) return;

    // EQ may be at index 0; Echo is next if enabled.
    UINT32 idx = (m_eq[(int)bus].enabled ? 1u : 0u);
    v->SetEffectParameters(idx, &p, sizeof(p));
}

void XAudioEngine::EnableMasterLimiter(bool enable) {
    if (!m_master) return;
    if (enable == m_masterLimiterEnabled) return;

    if (enable) {
        if (!m_masterLimiter) {
            IUnknown* fx = nullptr;
            if (SUCCEEDED(CreateFX(__uuidof(FXMasteringLimiter), &fx))) { // FXMasteringLimiter. :contentReference[oaicite:22]{index=22}
                XAUDIO2_VOICE_DETAILS md{}; m_master->GetVoiceDetails(&md);
                XAUDIO2_EFFECT_DESCRIPTOR d{}; d.InitialState = TRUE; d.OutputChannels = md.InputChannels; d.pEffect = fx;
                XAUDIO2_EFFECT_CHAIN chain{1, &d};
                m_master->SetEffectChain(&chain);
                m_masterLimiter = fx;
                m_masterLimiterEnabled = true;
            }
        }
    } else {
        // Clear effects on master
        XAUDIO2_EFFECT_CHAIN chain{0, nullptr};
        m_master->SetEffectChain(&chain);
        if (m_masterLimiter) { m_masterLimiter->Release(); m_masterLimiter = nullptr; }
        m_masterLimiterEnabled = false;
    }
}
#endif // CG_AUDIO_HAS_XAPOFX

