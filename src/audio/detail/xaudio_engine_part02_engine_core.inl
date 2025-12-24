// ============================ XAudioEngine ============================

XAudioEngine::~XAudioEngine() {
    Shutdown();
}

bool XAudioEngine::Init() {
    InitParams p{};
    return Init(p);
}

bool XAudioEngine::Init(const InitParams& p) {
    // Create engine
#if defined(_DEBUG) && (_WIN32_WINNT < 0x0602)
    UINT32 flags = XAUDIO2_DEBUG_ENGINE;
#else
    UINT32 flags = 0;
#endif

    HRESULT hr = XAudio2Create(m_xaudio.GetAddressOf(), flags, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !m_xaudio) return false; // XAudio2Create. :contentReference[oaicite:5]{index=5}

    hr = m_xaudio->CreateMasteringVoice(&m_master);
    if (FAILED(hr) || !m_master) return false;

    // Submix buses (SFX, Music, Ambience → Master)
    XAUDIO2_VOICE_DETAILS masterDetails{};
    m_master->GetVoiceDetails(&masterDetails);
    const UINT32 channels   = masterDetails.InputChannels;
    const UINT32 sampleRate = masterDetails.InputSampleRate;

    auto mkSubmix = [&](AudioBus bus) -> bool {
        IXAudio2SubmixVoice* v = nullptr;
        HRESULT r = m_xaudio->CreateSubmixVoice(&v, channels, sampleRate, 0, 0, nullptr, nullptr);
        if (FAILED(r) || !v) return false;
        m_submix[(int)bus] = v;
        return true;
    };

    if (!mkSubmix(AudioBus::SFX) || !mkSubmix(AudioBus::Music) || !mkSubmix(AudioBus::Ambience)) {
        Shutdown();
        return false;
    }

    // Initialize volumes/mutes/solos
    m_masterVol = 1.0f;
    if (m_master) m_master->SetVolume(m_masterVol);
    for (int i = 0; i < kBusCount; ++i) {
        if (m_submix[i]) m_submix[i]->SetVolume(1.0f);
        m_busVol[i]  = 1.0f;
        m_busMute[i] = false;
        m_busSolo[i] = false;
    }

#if CG_AUDIO_HAS_X3DAUDIO
    // Initialize X3DAudio using mastering voice channel mask, set speed of sound. :contentReference[oaicite:6]{index=6}
    if (p.enable3D) {
        DWORD chmask = 0;
        if (SUCCEEDED(m_master->GetChannelMask(&chmask))) { // GetChannelMask. :contentReference[oaicite:7]{index=7}
            m_masterChannelMask = chmask;
            X3DAudioInitialize(m_masterChannelMask, p.speedOfSound, m_x3dInstance); // X3DAudioInitialize. :contentReference[oaicite:8]{index=8}
            m_speedOfSound = p.speedOfSound;
        }
    }
#endif

#if CG_AUDIO_HAS_XAPOFX
    // Optional mastering limiter on Master
    if (p.enableLimiterOnMaster) {
        if (!m_masterLimiter) {
            IUnknown* fx = nullptr;
            if (SUCCEEDED(CreateFX(__uuidof(FXMasteringLimiter), &fx))) { // CreateFX. :contentReference[oaicite:9]{index=9}
                XAUDIO2_EFFECT_DESCRIPTOR d{}; d.InitialState = TRUE; d.OutputChannels = channels; d.pEffect = fx;
                XAUDIO2_EFFECT_CHAIN chain{1, &d};
                m_master->SetEffectChain(&chain);
                m_masterLimiter = fx;
                m_masterLimiterEnabled = true;
            }
        }
    }
#endif

    m_timeSec = 0.0;
    m_paused  = false;
    return true;
}

void XAudioEngine::Shutdown() {
    // Stop/destroy source voices first
    for (auto& kv : m_voicesById) {
        auto& inst = kv.second;
        if (inst.voice) {
            inst.voice->Stop(0);
            inst.voice->FlushSourceBuffers();
            inst.voice->DestroyVoice();
            inst.voice = nullptr;
        }
    }
    m_voicesById.clear();
    m_eventToVoiceIds.clear();
    m_activeAmbience.reset();
    m_prevAmbience.reset();

    // Destroy FX on submixes (XAudio2 takes ownership after SetEffectChain; but if we held IUnknown*, release to be safe)
#if CG_AUDIO_HAS_XAUDIO2FX
    for (int i = 0; i < kBusCount; ++i) {
        if (m_reverb[i].fx) { m_reverb[i].fx->Release(); m_reverb[i].fx = nullptr; }
        if (m_meter[i].fx)  { m_meter[i].fx->Release();  m_meter[i].fx  = nullptr; }
        m_reverb[i].enabled = false;
        m_meter[i].enabled  = false;
    }
#endif
#if CG_AUDIO_HAS_XAPOFX
    for (int i = 0; i < kBusCount; ++i) {
        if (m_eq[i].fx)   { m_eq[i].fx->Release();   m_eq[i].fx = nullptr; }
        if (m_echo[i].fx) { m_echo[i].fx->Release(); m_echo[i].fx = nullptr; }
        m_eq[i].enabled = false;
        m_echo[i].enabled = false;
    }
    if (m_masterLimiter) { m_masterLimiter->Release(); m_masterLimiter = nullptr; }
    m_masterLimiterEnabled = false;
#endif

    // Submix voices
    for (int i = 0; i < kBusCount; ++i) {
        if (m_submix[i]) {
            m_submix[i]->DestroyVoice();
            m_submix[i] = nullptr;
        }
    }

    if (m_master) {
        m_master->DestroyVoice();
        m_master = nullptr;
    }

    m_xaudio.Reset();
}

