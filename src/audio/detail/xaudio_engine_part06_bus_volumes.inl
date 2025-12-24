// --------------------------- Bus volumes / mutes / solos ---------------------------

static inline float EffectiveBusVol(float v, bool mute, bool anySolo, bool thisSolo) {
    if (mute) return 0.0f;
    if (anySolo && !thisSolo) return 0.0f;
    return v;
}

void XAudioEngine::SetBusVolume(AudioBus bus, float volume) {
    volume = Clamp(volume, 0.0f, 4.0f);
    if (bus == AudioBus::Master) {
        SetMasterVolume(volume);
        return;
    }
    m_busVol[(int)bus] = volume;

    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

float XAudioEngine::GetBusVolume(AudioBus bus) const {
    if (bus == AudioBus::Master) return m_masterVol;
    return m_busVol[(int)bus];
}

void XAudioEngine::SetMasterVolume(float volume) {
    m_masterVol = Clamp(volume, 0.0f, 4.0f);
    if (m_master) m_master->SetVolume(m_masterVol);
}

float XAudioEngine::GetMasterVolume() const {
    return m_masterVol;
}

void XAudioEngine::MuteBus(AudioBus bus, bool mute) {
    if (bus == AudioBus::Master) return;
    m_busMute[(int)bus] = mute;
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

bool XAudioEngine::IsBusMuted(AudioBus bus) const {
    if (bus == AudioBus::Master) return false;
    return m_busMute[(int)bus];
}

void XAudioEngine::SoloBus(AudioBus bus, bool solo) {
    if (bus == AudioBus::Master) return;
    m_busSolo[(int)bus] = solo;
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

bool XAudioEngine::IsBusSolo(AudioBus bus) const {
    if (bus == AudioBus::Master) return false;
    return m_busSolo[(int)bus];
}

