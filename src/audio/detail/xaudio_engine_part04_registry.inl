// --------------------------- Clip/Event Registry ---------------------------

bool XAudioEngine::RegisterClip(const std::string& id, const std::wstring& path) {
    if (id.empty()) return false;
    WavData wav{};
    if (!LoadWav(path, wav, nullptr)) return false;
    auto clip = std::make_shared<WavData>(std::move(wav));
    m_clips[id] = std::move(clip);
    return true;
}

void XAudioEngine::UnregisterClip(const std::string& id) {
    // NOTE: does not stop playing instances; call StopEvent before removing if needed
    m_clips.erase(id);
}

bool XAudioEngine::RegisterEvent(const std::string& name, const AudioEventDesc& desc) {
    if (name.empty()) return false;
    // Keep flexible: allow empty clip list for late binding (but Play will fail if unresolved)
    m_events[name] = desc;
    return true;
}

void XAudioEngine::UnregisterEvent(const std::string& name) {
    StopEvent(name, 0.05f);
    m_events.erase(name);
}

void XAudioEngine::PreloadEvent(const std::string& /*name*/) {
    // No-op for raw WAVs; placeholder for future streaming decode
}

