// --------------------------- Ducking API ---------------------------

void XAudioEngine::AddDuckingRule(AudioBus ducked, AudioBus ducker, float attenuationDb, float attackSec, float releaseSec) {
    m_duckRules.push_back(DuckRule{ ducked, ducker, attenuationDb, attackSec, releaseSec, 0.0f });
}

void XAudioEngine::ClearDuckingRules() {
    m_duckRules.clear();
}

} // namespace colony::audio
