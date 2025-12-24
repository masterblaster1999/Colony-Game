// --------------------------- 3D helpers ---------------------------

void XAudioEngine::Ensure3DInitialized() {
#if CG_AUDIO_HAS_X3DAUDIO
    if (m_masterChannelMask == 0 && m_master) {
        DWORD chmask = 0;
        if (SUCCEEDED(m_master->GetChannelMask(&chmask))) { // GetChannelMask. :contentReference[oaicite:27]{index=27}
            m_masterChannelMask = chmask;
            X3DAudioInitialize(m_masterChannelMask, m_speedOfSound, m_x3dInstance); // X3DAudioInitialize. :contentReference[oaicite:28]{index=28}
        }
    }
#endif
}

void XAudioEngine::Apply3DToVoice(VoiceInstance& inst) {
#if CG_AUDIO_HAS_X3DAUDIO
    if (!inst.voice) return;
    IXAudio2SubmixVoice* bus = BusToSubmix(inst.bus);
    if (!bus) return;

    XAUDIO2_VOICE_DETAILS src{}, dst{};
    inst.voice->GetVoiceDetails(&src);
    bus->GetVoiceDetails(&dst);

    X3DAUDIO_LISTENER lis{};
    lis.pCone = nullptr;
    lis.OrientFront = { m_listener.orientation.forward.x, m_listener.orientation.forward.y, m_listener.orientation.forward.z };
    lis.OrientTop   = { m_listener.orientation.up.x,      m_listener.orientation.up.y,      m_listener.orientation.up.z };
    lis.Position    = { m_listener.position.x, m_listener.position.y, m_listener.position.z };
    lis.Velocity    = { m_listener.velocity.x, m_listener.velocity.y, m_listener.velocity.z };

    X3DAUDIO_EMITTER em{};
    em.pCone = nullptr;
    em.OrientFront = { inst.emitter.orientation.forward.x, inst.emitter.orientation.forward.y, inst.emitter.orientation.forward.z };
    em.OrientTop   = { inst.emitter.orientation.up.x,      inst.emitter.orientation.up.y,      inst.emitter.orientation.up.z };
    em.Position    = { inst.emitter.position.x, inst.emitter.position.y, inst.emitter.position.z };
    em.Velocity    = { inst.emitter.velocity.x, inst.emitter.velocity.y, inst.emitter.velocity.z };
    em.ChannelCount = src.InputChannels;
    em.InnerRadius  = inst.emitter.innerRadius;
    em.InnerRadiusAngle = inst.emitter.innerRadiusAngle;
    em.CurveDistanceScaler = 1.0f;
    em.DopplerScaler = m_listener.dopplerScalar * inst.emitter.dopplerScalar;

    std::vector<float> matrix(src.InputChannels * dst.InputChannels, 0.0f);
    X3DAUDIO_DSP_SETTINGS dsp{};
    dsp.SrcChannelCount = src.InputChannels;
    dsp.DstChannelCount = dst.InputChannels;
    dsp.pMatrixCoefficients = matrix.data();

    DWORD flags = X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER;
    X3DAudioCalculate(m_x3dInstance, &lis, &em, flags, &dsp); // Integrate X3DAudio w/ XAudio2. :contentReference[oaicite:29]{index=29}

    // Apply doppler
    float ratio = std::clamp(dsp.DopplerFactor, kMinFreqRatio, kMaxFreqRatio);
    inst.voice->SetFrequencyRatio(ratio);

    // Apply panning/attenuation matrix
    inst.voice->SetOutputMatrix(bus, src.InputChannels, dst.InputChannels, matrix.data());
#else
    (void)inst;
#endif
}

