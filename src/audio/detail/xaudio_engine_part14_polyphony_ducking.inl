// --------------------------- Polyphony / Ducking / Filters ---------------------------

bool XAudioEngine::EnforcePolyphony(const std::string& eventName, const AudioEventDesc& desc, uint32_t& outStolenId) {
    outStolenId = 0;
    auto range = m_eventToVoiceIds.equal_range(eventName);
    std::vector<uint32_t> ids;
    for (auto it = range.first; it != range.second; ++it) ids.push_back(it->second);
    if (ids.size() < desc.maxPolyphony) return true;

    if (desc.steal == VoiceStealPolicy::None) return false;

    // Choose a victim
    uint32_t victim = 0;
    switch (desc.steal) {
        case VoiceStealPolicy::Oldest: {
            victim = *std::min_element(ids.begin(), ids.end()); // lower id ~ older
        } break;
        case VoiceStealPolicy::Newest: {
            victim = *std::max_element(ids.begin(), ids.end());
        } break;
        case VoiceStealPolicy::Quietest: {
            float minVol = std::numeric_limits<float>::max();
            for (uint32_t id : ids) {
                auto it = m_voicesById.find(id);
                if (it == m_voicesById.end()) continue;
                float v = it->second.currentVol * it->second.baseVolume * it->second.volumeScale;
                if (v < minVol) { minVol = v; victim = id; }
            }
        } break;
        default: break;
    }

    if (victim != 0) {
        auto it = m_voicesById.find(victim);
        if (it != m_voicesById.end()) {
            DestroyVoice(it->second);
            // unlink from event map
            auto r = m_eventToVoiceIds.equal_range(eventName);
            for (auto mit = r.first; mit != r.second; ) {
                if (mit->second == victim) mit = m_eventToVoiceIds.erase(mit);
                else ++mit;
            }
            m_voicesById.erase(it);
            outStolenId = victim;
            return true;
        }
    }
    return false;
}

void XAudioEngine::UpdateDucking(float dt) {
    if (m_duckRules.empty()) return;

    // Simple activity heuristic: if any voice exists on 'ducker' bus, target=1 else 0; attack/release smoothing.
    bool busActive[kBusCount] = {false,false,false,false};
    for (auto& kv : m_voicesById) {
        if (!kv.second.voice) continue;
        busActive[(int)kv.second.bus] = true;
    }

    bool anySolo = std::any_of(std::begin(m_busSolo), std::end(m_busSolo), [](bool s){return s;});

    for (auto& rule : m_duckRules) {
        float target = busActive[(int)rule.ducker] ? 1.0f : 0.0f;
        float tau = (target > rule.env) ? std::max(1e-4f, rule.attackSec) : std::max(1e-4f, rule.releaseSec);
        float k = Clamp(dt / tau, 0.0f, 1.0f);
        rule.env = Lerp(rule.env, target, k);

        float duckGain = DbToLin(-std::abs(rule.attenDb) * rule.env);
        int duckedIdx = (int)rule.ducked;

        // Apply combined bus volume with ducking
        if (m_submix[duckedIdx]) {
            float eff = EffectiveBusVol(m_busVol[duckedIdx], m_busMute[duckedIdx], anySolo, m_busSolo[duckedIdx]);
            m_submix[duckedIdx]->SetVolume(eff * duckGain);
        }
    }
}

void XAudioEngine::SetVoiceFilter(IXAudio2SourceVoice* v, XAUDIO2_FILTER_TYPE type, float cutoffHz, float oneOverQ) {
    if (!v) return;
    XAUDIO2_VOICE_DETAILS vd{}; v->GetVoiceDetails(&vd);
    XAUDIO2_FILTER_PARAMETERS fp{};
    fp.Type = type;
    if (type == LowPassFilter || type == HighPassFilter) {
        fp.Frequency = (cutoffHz <= 0.0f) ? 0.0f : XAudio2CutoffFrequencyToOnePoleCoefficient(cutoffHz, vd.InputSampleRate); // one-pole helper. :contentReference[oaicite:30]{index=30}
        fp.OneOverQ = 1.0f;
    } else {
        fp.Frequency = (cutoffHz <= 0.0f) ? 0.0f : XAudio2CutoffFrequencyToRadians(cutoffHz, vd.InputSampleRate);            // radians helper for SVF. :contentReference[oaicite:31]{index=31}
        fp.OneOverQ = std::max(1e-3f, oneOverQ);
    }
    v->SetFilterParameters(&fp);
}

void XAudioEngine::ApplyOcclusion(VoiceInstance& inst) {
    if (!inst.voice || !inst.is3D) return;
    float occ = Clamp01(inst.emitter.occlusion + inst.emitter.obstruction);
    if (occ <= 0.0f) {
        // Clear LPF
        SetVoiceFilter(inst.voice, LowPassFilter, 0.0f, 1.0f);
        return;
    }
    // Map to cutoff & gain
    float cut = Lerp(m_occMaxCutHz, m_occMinCutHz, occ);
    float g   = Lerp(m_occMaxGain,  m_occMinGain,  occ);
    SetVoiceFilter(inst.voice, LowPassFilter, cut, 1.0f);
    inst.volumeScale = std::max(0.0f, g); // apply gain scaling via volumeScale (transient)
}

