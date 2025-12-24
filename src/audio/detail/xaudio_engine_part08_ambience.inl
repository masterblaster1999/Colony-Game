// --------------------------- Ambience ---------------------------

void XAudioEngine::RegisterAmbience(Biome biome, Climate climate, const std::string& eventName) {
    m_ambienceMap[{biome, climate}] = eventName;
}

void XAudioEngine::ClearAmbienceMap() {
    m_ambienceMap.clear();
}

void XAudioEngine::SetAmbience(Biome biome, Climate climate, float crossfadeSec) {
    auto it = m_ambienceMap.find({biome, climate});
    if (it == m_ambienceMap.end()) return;
    SetAmbienceByEvent(it->second, crossfadeSec);
}

void XAudioEngine::SetAmbienceByEvent(const std::string& eventName, float crossfadeSec) {
    // If already active with same event, ignore
    if (m_activeAmbience && m_voicesById.count(m_activeAmbience->id)) {
        auto& inst = m_voicesById[m_activeAmbience->id];
        if (inst.eventName == eventName) return;
    }

    // Start new ambience (must be registered & looping)
    auto evIt = m_events.find(eventName);
    if (evIt == m_events.end()) return;

    AudioEventDesc desc = evIt->second;
    desc.bus = AudioBus::Ambience;
    desc.loop = true;
    desc.fades.inSec = std::max(0.0f, crossfadeSec);

    auto* newInst = PlayInternal(desc, eventName, 1.0f, 0.0f, nullptr);
    if (!newInst) return;

    // Fade out previous ambience
    if (m_activeAmbience && m_voicesById.count(m_activeAmbience->id)) {
        auto prev = *m_activeAmbience;
        Stop(prev, crossfadeSec);
        m_prevAmbience = prev;
    } else {
        m_prevAmbience.reset();
    }

    m_activeAmbience = AudioEventHandle{newInst->id};
}

