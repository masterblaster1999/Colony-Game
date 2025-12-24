// --------------------------- Update/Transport/Scheduling ---------------------------

void XAudioEngine::Pause(bool pause) {
    if (m_paused == pause) return;
    m_paused = pause;
    for (auto& kv : m_voicesById) {
        if (!kv.second.voice) continue;
        if (pause) kv.second.voice->Stop(0);
        else       kv.second.voice->Start(0);
    }
}

bool XAudioEngine::IsPaused() const { return m_paused; }

uint64_t XAudioEngine::SchedulePlay(const std::string& eventName, double delaySec, float volumeScale, float pitchSemitoneOffset) {
    Scheduled s{};
    s.id = m_nextScheduleId++;
    s.triggerTime = m_timeSec + std::max(0.0, delaySec);
    s.eventName = eventName;
    s.volumeScale = volumeScale;
    s.pitchSemitones = pitchSemitoneOffset;
    // keep deque sorted by trigger time (stable insert)
    auto it = std::upper_bound(m_schedule.begin(), m_schedule.end(), s.triggerTime,
        [](double t, const Scheduled& a) { return t < a.triggerTime; });
    m_schedule.insert(it, std::move(s));
    return s.id;
}

void XAudioEngine::CancelScheduled(uint64_t scheduleId) {
    auto it = std::find_if(m_schedule.begin(), m_schedule.end(), [&](const Scheduled& s){ return s.id == scheduleId; });
    if (it != m_schedule.end()) m_schedule.erase(it);
}

void XAudioEngine::UpdateScheduling(double dt) {
    if (m_paused) return;
    m_timeSec += dt;
    while (!m_schedule.empty() && m_schedule.front().triggerTime <= m_timeSec) {
        Scheduled s = m_schedule.front();
        m_schedule.pop_front();

        auto evIt = m_events.find(s.eventName);
        if (evIt == m_events.end()) continue;

        if (s.emitter3D.has_value()) {
            Play3D(s.eventName, *s.emitter3D, s.volumeScale, s.pitchSemitones);
        } else {
            Play(s.eventName, s.volumeScale, s.pitchSemitones);
        }
    }
}

void XAudioEngine::Update(float dtSeconds) {
    UpdateScheduling(dtSeconds);

    // Tick fades / 3D / pan
    for (auto& kv : m_voicesById) {
        TickVoice(kv.second, dtSeconds);
    }

    // Ducking after per-voice volume set (applies to submixes)
    UpdateDucking(dtSeconds);

    // Reap finished voices and invoke callback if any
    ReapFinishedVoices();

    // If previous ambience finished, clear handle
    if (m_prevAmbience && !m_voicesById.count(m_prevAmbience->id)) {
        m_prevAmbience.reset();
    }
}

