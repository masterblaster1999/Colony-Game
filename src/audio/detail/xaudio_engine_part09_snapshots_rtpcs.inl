// --------------------------- Snapshots / RTPCs / Occlusion ---------------------------

XAudioEngine::Snapshot XAudioEngine::CaptureSnapshot() const {
    Snapshot s{};
    for (int i = 0; i < kBusCount; ++i) {
        s.busVolumes[i] = m_busVol[i];
        s.busMutes[i]   = m_busMute[i];
        s.busSolos[i]   = m_busSolo[i];
    }
    return s;
}

void XAudioEngine::ApplySnapshot(const Snapshot& s, float /*fadeSec*/) {
    for (int i = 0; i < kBusCount; ++i) {
        m_busVol[i]  = Clamp(s.busVolumes[i], 0.0f, 4.0f);
        m_busMute[i] = s.busMutes[i];
        m_busSolo[i] = s.busSolos[i];
    }
    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool v){return v;});
    for (int i = 0; i < kBusCount; ++i) {
        if (!m_submix[i]) continue;
        float eff = EffectiveBusVol(m_busVol[i], m_busMute[i], anySolo, m_busSolo[i]);
        m_submix[i]->SetVolume(eff);
    }
}

void XAudioEngine::SetRTPC(const std::string& name, std::function<void(float)> fn) {
    if (name.empty()) return;
    m_rtpcs[name] = std::move(fn);
}

void XAudioEngine::RemoveRTPC(const std::string& name) {
    m_rtpcs.erase(name);
}

void XAudioEngine::UpdateRTPC(const std::string& name, float value) {
    auto it = m_rtpcs.find(name);
    if (it != m_rtpcs.end() && it->second) it->second(value);
}

void XAudioEngine::ClearRTPCs() {
    m_rtpcs.clear();
}

void XAudioEngine::SetOcclusionMapping(float minCutoffHz, float maxCutoffHz, float minGainLinear, float maxGainLinear) {
    m_occMinCutHz = std::max(10.0f, minCutoffHz);
    m_occMaxCutHz = std::max(m_occMinCutHz, maxCutoffHz);
    m_occMinGain  = Clamp(minGainLinear, 0.0f, 1.0f);
    m_occMaxGain  = Clamp(maxGainLinear, 0.0f, 1.0f);
    if (m_occMaxGain < m_occMinGain) std::swap(m_occMaxGain, m_occMinGain);
}

