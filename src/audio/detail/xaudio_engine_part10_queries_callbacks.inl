// --------------------------- Queries / Callbacks ---------------------------

uint32_t XAudioEngine::GetActiveInstanceCount(const std::string& eventName) const {
    auto range = m_eventToVoiceIds.equal_range(eventName);
    return static_cast<uint32_t>(std::distance(range.first, range.second));
}

uint32_t XAudioEngine::GetTotalActiveInstances() const {
    return static_cast<uint32_t>(m_voicesById.size());
}

PerformanceData XAudioEngine::GetPerformanceData() const {
    PerformanceData out{};
    if (!m_xaudio) return out;

    XAUDIO2_PERFORMANCE_DATA pd{};
    m_xaudio->GetPerformanceData(&pd); // GetPerformanceData. :contentReference[oaicite:23]{index=23}

    // Fields available in XAudio2 2.9 (Windows 10/11 SDK). 'TotalVoiceCount' no longer exists.
    out.activeSourceVoiceCount    = pd.ActiveSourceVoiceCount;
    // Derive an approximate "total voices" = sources + submixes + master
    uint32_t derivedTotalVoices   = pd.ActiveSourceVoiceCount + pd.ActiveSubmixVoiceCount + 1u;
    out.totalVoices               = derivedTotalVoices;

    out.audioCyclesSinceLastQuery = pd.AudioCyclesSinceLastQuery;
    out.totalCyclesSinceLastQuery = pd.TotalCyclesSinceLastQuery;
    out.memoryUsageBytes          = pd.MemoryUsageInBytes;
    out.currentLatencySamples     = pd.CurrentLatencyInSamples;
    return out;
}

void XAudioEngine::SetOnEventEnd(OnEventEnd cb) { m_onEventEnd = std::move(cb); }

