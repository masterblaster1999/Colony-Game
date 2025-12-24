// src/slice/ObjectiveTracker.cpp
// Implementation for the Objective & Achievement System runtime.

#include "ObjectiveTracker.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <limits>
#include <sstream>

namespace slice {

// ---------------- Lifecycle ----------------

void ObjectiveTracker::reset() {
    SLICE_OT_LOCK_GUARD;
    state_.clear();
    totalScore_ = 0;
    index_ = static_cast<std::size_t>(-1);
    lastCheckpoint_.reset();
    lastProgress_.assign(objectives_.size(), 0.0f);
#if !SLICE_OT_ENABLE_TELEMETRY
    counterSnapshots_.clear();
#endif
    for (auto& o : objectives_) {
        o.status = Status::Locked;
        o.activatedAt = 0.0;
        o.completedAt = 0.0;
        o.lastFailReason.clear();
        o.repeatCountProgress = 0;
        for (auto& so : o.subs) {
            so.status = Status::Locked;
            so.activatedAt = 0.0;
        }
    }
#if SLICE_OT_ENABLE_TELEMETRY
    log_.clear();
#endif
}

void ObjectiveTracker::start() {
    SLICE_OT_LOCK_GUARD;
    if (objectives_.empty()) return;
    // Find first enabled objective
    index_ = firstEnabledIndexFrom_(0);
    if (index_ < objectives_.size()) {
        activateObjective_(index_);
    }
}

void ObjectiveTracker::pause(bool v) {
    SLICE_OT_LOCK_GUARD;
    state_.paused = v;
}

void ObjectiveTracker::resume() { pause(false); }

void ObjectiveTracker::setTimeScale(double s) {
    SLICE_OT_LOCK_GUARD;
    state_.timeScale = (s > 0 ? s : 0);
}

void ObjectiveTracker::setLocalizer(LocalizeFn fn) {
    SLICE_OT_LOCK_GUARD;
    localize_ = std::move(fn);
}

void ObjectiveTracker::setHudTokenPrefix(char ch) {
    SLICE_OT_LOCK_GUARD;
    locPrefix_ = ch;
}

void ObjectiveTracker::update(double dtSeconds) {
    SLICE_OT_LOCK_GUARD;
    if (isComplete() || isFailed()) return;
    if (!state_.paused) state_.elapsedSeconds += dtSeconds * state_.timeScale;
    advance_();
}

// ---------------- Build & query ----------------

Objective& ObjectiveTracker::add(Objective o) {
    SLICE_OT_LOCK_GUARD;
    objectives_.push_back(std::move(o));
    lastProgress_.push_back(0.0f);
    return objectives_.back();
}

Objective& ObjectiveTracker::add(Objective::Builder b) { return add(b.build()); }

const Objective* ObjectiveTracker::current() const {
    return isComplete() ? nullptr : &objectives_[index_];
}

std::optional<std::size_t> ObjectiveTracker::findById(std::string_view id) const {
    for (std::size_t i = 0; i < objectives_.size(); ++i) {
        if (objectives_[i].id == id) return i;
    }
    return std::nullopt;
}

bool ObjectiveTracker::setEnabledById(std::string_view id, bool enabled) {
    SLICE_OT_LOCK_GUARD;
    auto idx = findById(id);
    if (!idx) return false;
    objectives_[*idx].enabled = enabled;
    return true;
}

bool ObjectiveTracker::skipCurrent() {
    SLICE_OT_LOCK_GUARD;
    if (isComplete()) return false;
    auto& cur = objectives_[index_];
    const Status old = cur.status;
    cur.status = Status::Skipped;
    fireStatus_(cur, old, cur.status);
    moveToNext_(/*branchFrom*/std::nullopt, /*onFail*/false);
    return true;
}

bool ObjectiveTracker::failCurrent(std::string reason) {
    SLICE_OT_LOCK_GUARD;
    if (isComplete()) return false;
    auto& cur = objectives_[index_];
    if (cur.status != Status::Active) return false;
    setFail_(cur, std::move(reason));
    moveToNext_(/*branchFrom*/index_, /*onFail*/true);
    return true;
}

bool ObjectiveTracker::setCurrentById(std::string_view id) {
    SLICE_OT_LOCK_GUARD;
    auto idx = findById(id);
    if (!idx) return false;
    index_ = *idx;
    activateObjective_(index_);
    return true;
}

// ---------------- Event API ----------------

void ObjectiveTracker::notifyStructureBuilt(int count) {
    SLICE_OT_LOCK_GUARD;
    state_.structuresBuilt += count;
    notifyCounterImpl_("structures.built", count);
}

void ObjectiveTracker::notifyItemCrafted(int count) {
    SLICE_OT_LOCK_GUARD;
    state_.itemsCrafted += count;
    notifyCounterImpl_("items.crafted", count);
}

void ObjectiveTracker::notifyColonistSpawned(int count) {
    SLICE_OT_LOCK_GUARD;
    state_.colonistsAlive += count;
}

void ObjectiveTracker::notifyColonistDied(int count) {
    SLICE_OT_LOCK_GUARD;
    state_.colonistsAlive -= count;
    if (state_.colonistsAlive <= 0) state_.lost = true;
    notifyEventUnlocked_("colonist.death", 1);
    advance_();
}

void ObjectiveTracker::notifyCounter(std::string_view name, std::int64_t delta) {
    SLICE_OT_LOCK_GUARD;
    notifyCounterImpl_(name, delta);
}

void ObjectiveTracker::setCounter(std::string_view name, std::int64_t value) {
    SLICE_OT_LOCK_GUARD;
    auto& v = state_.counters[std::string(name)];
    auto delta = value - v;
    v = value;
    logEvent_(makeKey_("ctr:", name), delta, v);
    advance_();
}

std::int64_t ObjectiveTracker::getCounter(std::string_view name) const {
    auto it = state_.counters.find(std::string(name));
    return (it == state_.counters.end()) ? 0 : it->second;
}

void ObjectiveTracker::setFlag(std::string_view name, bool value) {
    SLICE_OT_LOCK_GUARD;
    const std::string k(name);
    bool changed = false;
    if (value) {
        changed = state_.flags.insert(k).second;
    } else {
        changed = state_.flags.erase(k) > 0;
    }
    if (changed) logEvent_(makeKey_("flg:", k), value ? 1 : -1, value ? 1 : 0);
    advance_();
}

bool ObjectiveTracker::getFlag(std::string_view name) const {
    return state_.flags.count(std::string(name)) != 0;
}

void ObjectiveTracker::notifyEvent(std::string_view eventName, std::int64_t count) {
    SLICE_OT_LOCK_GUARD;
    notifyEventUnlocked_(eventName, count);
    advance_();
}

// ---------------- HUD / Progress ----------------

std::vector<std::string> ObjectiveTracker::hudLines() const {
    return hudLines(HudOptions{});
}

std::vector<std::string> ObjectiveTracker::hudLines(const HudOptions& opt) const {
    std::vector<std::string> out;
    if (isComplete()) {
        out.emplace_back("Vertical Slice: COMPLETE (Score: " + std::to_string(totalScore_) + ")");
        return out;
    }
    if (isFailed()) {
        const auto* c = current();
        out.emplace_back("Vertical Slice: FAILED — " + (c ? c->lastFailReason : "unknown"));
        return out;
    }

    const Objective* cur = current();
    SLICE_OT_ASSERT(cur);
    // reserve a bit to avoid re-allocations on every call
    out.reserve(16 + cur->criteria.size() + cur->subs.size() * 3);

    out.emplace_back("Objective: " + loc_(cur->title) + (opt.showCompletedCheck ? "  [ ]" : ""));
    if (!cur->description.empty()) out.emplace_back(loc_(cur->description));

    // Own criteria
    for (const auto& c : cur->criteria) {
        if (c.hiddenInHud) continue;
        const float p = criterionProgress_(c, cur->activatedAt);
        const std::string label = hudLabel_(c);
        out.emplace_back(" - " + label + ": " + progressBar_(p, opt.barWidth) + " " + percent_(p) +
                         extraDetail_(c, cur->activatedAt));
    }

    // Sub-objectives
    if (opt.showSubObjectives && !cur->subs.empty()) {
        out.emplace_back("Sub-objectives:");
        for (const auto& so : cur->subs) {
            out.emplace_back("  - " + loc_(so.title) + statusTag_(so.status));
            for (const auto& c : so.criteria) {
                if (c.hiddenInHud) continue;
                const float p = criterionProgress_(c, so.activatedAt);
                const std::string label = hudLabel_(c);
                out.emplace_back("      • " + label + ": " + progressBar_(p, opt.barWidth) + " " + percent_(p) +
                                 extraDetail_(c, so.activatedAt));
            }
        }
    }

    if (opt.showCounters) {
        out.emplace_back("Built: " + std::to_string(state_.structuresBuilt) +
                         "   Crafted: " + std::to_string(state_.itemsCrafted));
    }
    out.emplace_back(std::string("Colonists: ") + std::to_string(state_.colonistsAlive) + (state_.lost ? " (LOST)" : ""));
    if (opt.showTimer) out.emplace_back("Survival: " + mmss_(state_.elapsedSeconds));
    return out;
}

double ObjectiveTracker::overallProgress() const {
    if (objectives_.empty()) return 1.0;
    double sumW = 0.0, sumP = 0.0;
    for (std::size_t i = 0; i < objectives_.size(); ++i) {
        const auto& o = objectives_[i];
        const double w = (o.weight > 0.0 ? o.weight : 1.0);
        sumW += w;
        double p = 0.0;
        switch (o.status) {
            case Status::Completed: p = 1.0; break;
            case Status::Active: p = objectiveProgress_(o); break;
            default: p = 0.0; break;
        }
        sumP += w * p;
    }
    return (sumW > 0.0 ? sumP / sumW : 0.0);
}

// ---------------- Observability ----------------

void ObjectiveTracker::setProgressCallback(ProgressCallback cb) {
    SLICE_OT_LOCK_GUARD;
    onProgress_ = std::move(cb);
}

void ObjectiveTracker::setStatusCallback(StatusCallback cb) {
    SLICE_OT_LOCK_GUARD;
    onStatus_ = std::move(cb);
}

bool ObjectiveTracker::addProgressThresholdWatcherById(std::string_view id, double threshold, ThresholdCallback cb) {
    SLICE_OT_LOCK_GUARD;
    auto idx = findById(id);
    if (!idx) return false;
    threshold = std::clamp(threshold, 0.0, 1.0);
    watchers_.push_back({ *idx, threshold, false, std::move(cb) });
    return true;
}

// ---------------- Checkpoints ----------------

bool ObjectiveTracker::restoreToLastCheckpoint() {
    SLICE_OT_LOCK_GUARD;
    if (!lastCheckpoint_ || *lastCheckpoint_ >= objectives_.size()) return false;
    for (std::size_t i = *lastCheckpoint_; i < objectives_.size(); ++i) {
        auto& o = objectives_[i];
        o.status = Status::Locked;
        o.activatedAt = 0.0;
        o.completedAt = 0.0;
        o.lastFailReason.clear();
        o.repeatCountProgress = 0;
        for (auto& so : o.subs) {
            so.status = Status::Locked;
            so.activatedAt = 0.0;
        }
    }
    index_ = *lastCheckpoint_;
    activateObjective_(index_);
    return true;
}

// ---------------- Save / Load ----------------

std::string ObjectiveTracker::serialize() const {
    std::ostringstream oss;
    oss << "OT3 " << SLICE_OT_VERSION_MAJOR << "." << SLICE_OT_VERSION_MINOR << "\n";
    oss << "time " << state_.elapsedSeconds << " scale " << state_.timeScale << " paused " << (state_.paused ? 1 : 0) << "\n";
    oss << "sb " << state_.structuresBuilt << " ic " << state_.itemsCrafted << " ca " << state_.colonistsAlive << " lost " << (state_.lost ? 1 : 0)
        << "\n";
    oss << "score " << totalScore_ << " idx " << index_ << " chk " << (lastCheckpoint_ ? int(*lastCheckpoint_) : -1) << "\n";

    for (const auto& kv : state_.counters) oss << "ctr " << kv.first << " " << kv.second << "\n";
    for (const auto& f : state_.flags) oss << "flg " << f << "\n";

    for (std::size_t i = 0; i < objectives_.size(); ++i) {
        const auto& o = objectives_[i];
        oss << "obj " << i << " " << o.id << " " << int(o.status) << " " << o.repeatCountProgress << " " << o.activatedAt << " " << o.completedAt
            << "\n";
        for (std::size_t j = 0; j < o.subs.size(); ++j) {
            const auto& so = o.subs[j];
            oss << "sub " << i << " " << j << " " << so.id << " " << int(so.status) << " " << so.activatedAt << "\n";
        }
    }
    return oss.str();
}

bool ObjectiveTracker::deserialize(std::string_view data) {
    SLICE_OT_LOCK_GUARD;

    // Robust parsing: use a real istringstream instance.
    std::istringstream iss{ std::string(data) };

    std::string tag;
    int vmaj = 0, vmin = 0;
    if (!(iss >> tag)) return false;

    if (tag == "OT3") {
        char dot;
        iss >> vmaj >> dot >> vmin;
    } else if (tag == "OT2" || tag == "OT1") {
        /* accept older, ignore extra */
    } else {
        return false;
    }

    state_.clear();
    totalScore_ = 0;
    index_ = static_cast<std::size_t>(-1);
    lastCheckpoint_.reset();
    lastProgress_.assign(objectives_.size(), 0.0f);
#if !SLICE_OT_ENABLE_TELEMETRY
    counterSnapshots_.clear();
#endif

    for (auto& o : objectives_) {
        o.status = Status::Locked;
        o.activatedAt = 0.0;
        o.completedAt = 0.0;
        o.lastFailReason.clear();
        o.repeatCountProgress = 0;
        for (auto& so : o.subs) {
            so.status = Status::Locked;
            so.activatedAt = 0.0;
        }
    }

    while (iss >> tag) {
        if (tag == "time") {
            iss >> state_.elapsedSeconds;
        } else if (tag == "scale") {
            iss >> state_.timeScale;
        } else if (tag == "paused") {
            int p;
            iss >> p;
            state_.paused = (p != 0);
        } else if (tag == "sb") {
            iss >> state_.structuresBuilt;
        } else if (tag == "ic") {
            iss >> state_.itemsCrafted;
        } else if (tag == "ca") {
            iss >> state_.colonistsAlive;
        } else if (tag == "lost") {
            int b;
            iss >> b;
            state_.lost = (b != 0);
        } else if (tag == "score") {
            iss >> totalScore_;
        } else if (tag == "idx") {
            std::size_t i;
            iss >> i;
            index_ = i;
        } else if (tag == "chk") {
            int ci;
            iss >> ci;
            if (ci >= 0) lastCheckpoint_ = std::size_t(ci);
        } else if (tag == "ctr") {
            std::string k;
            std::int64_t v;
            iss >> k >> v;
            state_.counters[k] = v;
        } else if (tag == "flg") {
            std::string k;
            iss >> k;
            state_.flags.insert(k);
        } else if (tag == "obj") {
            std::size_t i;
            std::string id;
            int st;
            int rpt;
            double act, comp;
            iss >> i >> id >> st >> rpt >> act >> comp;
            if (i < objectives_.size() && objectives_[i].id == id) {
                auto& o = objectives_[i];
                o.status = Status(st);
                o.repeatCountProgress = rpt;
                o.activatedAt = act;
                o.completedAt = comp;
            }
        } else if (tag == "sub") {
            std::size_t oi, si;
            std::string id;
            int st;
            double act;
            iss >> oi >> si >> id >> st >> act;
            if (oi < objectives_.size() && si < objectives_[oi].subs.size() && objectives_[oi].subs[si].id == id) {
                auto& so = objectives_[oi].subs[si];
                so.status = Status(st);
                so.activatedAt = act;
            }
        }
    }

    if (!objectives_.empty()) {
        if (index_ >= objectives_.size()) {
            index_ = objectives_.size();
        } else if (!isComplete() && objectives_[index_].status != Status::Active) {
            activateObjective_(index_);
        }
    }
    (void)vmaj;
    (void)vmin;
    return true;
}

// ---------------- Pre-canned default slice ----------------

ObjectiveTracker ObjectiveTracker::MakeDefault(double surviveSeconds, int structuresToBuild, int itemsToCraft, int startingColonists) {
    ObjectiveTracker t;
    t.state_.colonistsAlive = startingColonists;

    t.add(Objective::Builder("build")
              .title("$EstablishColony")
              .desc("$BuildDesc")
              .allOf({ Criterion::counterAtLeast("structures.built", structuresToBuild, "$BuildStructures") })
              .reward(50)
              .weight(1.0)
              .markCheckpoint(true));

    t.add(Objective::Builder("craft")
              .title("$EnableProduction")
              .desc("$CraftDesc")
              .allOf({ Criterion::counterAtLeast("items.crafted", itemsToCraft, "$CraftItems") })
              .reward(50)
              .weight(1.0));

    t.add(Objective::Builder("survive")
              .title("$WeatherTheNight")
              .desc("$SurviveDesc")
              .allOf({ Criterion::timeElapsed(surviveSeconds, "$SurviveTimer") })
              .subAll({ SubObjective::Builder("no-death-60s")
                            .title("$NoDeaths60s")
                            .allOf({ Criterion::noEventInWindow("colonist.death", 60.0, "$NoRecentDeaths") })
                            .build() })
              .minColonists(1)
              .reward(100)
              .markCheckpoint(true));

    t.add(Objective::Builder("endcheck")
              .title("$KeepThemAlive")
              .desc("$EndWith3Colonists")
              .allOf({ Criterion::predicateFn([](const SliceState& s) { return s.colonistsAlive >= 3; }, "$ColonistsGte3") })
              .reward(100));

    t.start();
    return t;
}

#if SLICE_OT_ENABLE_TELEMETRY
std::vector<ObjectiveTracker::EventRecord> ObjectiveTracker::recentEvents(std::size_t maxCount) const {
    std::vector<EventRecord> v;
    const std::size_t n = std::min(maxCount, log_.size());
    v.reserve(n);
    for (std::size_t i = log_.size() - n; i < log_.size(); ++i) v.push_back(log_[i]);
    return v;
}
#endif

// ============================= Implementation ================================

std::string ObjectiveTracker::makeKey_(std::string_view ns, std::string_view name) {
    std::string s;
    s.reserve(ns.size() + name.size());
    s.append(ns);
    s.append(name);
    return s;
}

// -------- Activation & sequencing --------

void ObjectiveTracker::activateObjective_(std::size_t idx) {
    SLICE_OT_ASSERT(idx < objectives_.size());
    auto& o = objectives_[idx];
    if (!o.enabled) {
        // skip disabled to next enabled
        moveToNext_(/*branchFrom*/std::nullopt, /*onFail*/false);
        return;
    }
    Status old = o.status;
    o.status = Status::Active;
    o.activatedAt = state_.elapsedSeconds;

    // activate subs
    for (auto& so : o.subs) {
        so.status = Status::Active;
        so.activatedAt = o.activatedAt;
        if (so.onActivate) so.onActivate(state_);
    }

    // capture snapshots for delta-since-activation (when telemetry disabled)
    captureSnapshotsFor_(idx);

    if (o.onActivate) o.onActivate(state_);
    fireStatus_(o, old, o.status);
    fireProgressWithWatchers_(o, objectiveProgress_(o));
}

void ObjectiveTracker::moveToNext_(std::optional<std::size_t> branchFrom, bool onFail) {
    // Branching by id?
    if (branchFrom) {
        const auto& from = objectives_[*branchFrom];
        const std::string& targetId = onFail ? from.nextOnFailId : from.nextOnCompleteId;
        if (!targetId.empty()) {
            auto idx = findById(targetId);
            if (idx && *idx < objectives_.size()) {
                index_ = *idx;
                activateObjective_(index_);
                return;
            }
        }
    }

    // Linear advance: next enabled objective
    index_ = firstEnabledIndexFrom_(index_ + 1);
    if (!isComplete()) activateObjective_(index_);
}

std::size_t ObjectiveTracker::firstEnabledIndexFrom_(std::size_t start) const {
    for (std::size_t i = start; i < objectives_.size(); ++i) {
        if (objectives_[i].enabled) return i;
    }
    return objectives_.size();
}

void ObjectiveTracker::advance_() {
    if (isComplete()) return;

    while (!isComplete()) {
        auto& cur = objectives_[index_];

        // Fail checks:
        if (cur.failIfLost && state_.lost) {
            setFail_(cur, "all-colonists-lost");
            moveToNext_(index_, /*onFail*/true);
            break;
        }
        if (cur.minColonistsAlive && state_.colonistsAlive < *cur.minColonistsAlive) {
            setFail_(cur, "min-colonists");
            moveToNext_(index_, true);
            break;
        }
        if (cur.timeoutSeconds && (state_.elapsedSeconds - cur.activatedAt) > *cur.timeoutSeconds) {
            setFail_(cur, "timeout");
            moveToNext_(index_, true);
            break;
        }

        // Evaluate:
        const bool selfOk = evalCriteria_(cur.criteria, cur.logic, cur.activatedAt);
        const bool subsOk = evalSubs_(cur);

        if (!(selfOk && subsOk)) {
            fireProgressWithWatchers_(cur, objectiveProgress_(cur));
            break;
        }

        // Completed a *cycle* of this objective
        cur.repeatCountProgress += 1;
        if (cur.repeatCountProgress < cur.repeatCountTarget) {
            // Repeat: re-arm without leaving step
            if (cur.onComplete) cur.onComplete(state_);
            fireStatus_(cur, Status::Active, Status::Active); // stay active
            // Reset timers & subs for next repetition
            cur.activatedAt = state_.elapsedSeconds;
            for (auto& so : cur.subs) {
                so.status = Status::Active;
                so.activatedAt = cur.activatedAt;
            }
            captureSnapshotsFor_(index_);
            fireProgressWithWatchers_(cur, objectiveProgress_(cur));
            break;
        }

        // Fully complete objective
        const Status old = cur.status;
        cur.status = Status::Completed;
        cur.completedAt = state_.elapsedSeconds;
        totalScore_ += cur.scoreReward;
        if (cur.onComplete) cur.onComplete(state_);
        fireStatus_(cur, old, cur.status);
        if (cur.checkpoint) lastCheckpoint_ = index_;

        moveToNext_(index_, /*onFail*/false);
        if (isComplete()) break;
    }
}

// -------- Evaluation --------

bool ObjectiveTracker::evalCriteria_(const std::vector<Criterion>& list, Logic logic, double activatedAt) const {
    if (list.empty()) return true;
    if (logic == Logic::All) {
        for (const auto& c : list) {
            if (!criterionSatisfied_(c, activatedAt)) return false;
        }
        return true;
    }

    for (const auto& c : list) {
        if (criterionSatisfied_(c, activatedAt)) return true;
    }
    return false;
}

bool ObjectiveTracker::evalSubs_(Objective& o) {
    if (o.subs.empty()) return true;
    int completed = 0;
    for (auto& so : o.subs) {
        if (so.status == Status::Completed) {
            ++completed;
            continue;
        }
        const bool ok = evalCriteria_(so.criteria, so.logic, so.activatedAt);
        if (ok && so.status == Status::Active) {
            so.status = Status::Completed;
            if (so.onComplete) so.onComplete(state_);
        }
        if (so.status == Status::Completed) ++completed;
    }

    if (o.subLogic == Logic::All) return completed == int(o.subs.size());
    const int need = (o.minSubsToComplete > 0 ? o.minSubsToComplete : 1);
    return completed >= need;
}

bool ObjectiveTracker::criterionSatisfied_(const Criterion& c, double activatedAt) const {
    bool result = false;
    switch (c.kind) {
        case Criterion::Kind::CounterAtLeast: result = (readCounter_(c.key) >= c.target); break;
        case Criterion::Kind::CounterAtMost: result = (readCounter_(c.key) <= c.target); break;
        case Criterion::Kind::CounterEqual: result = (readCounter_(c.key) == c.target); break;
        case Criterion::Kind::CounterRange: {
            auto v = readCounter_(c.key);
            result = (v >= c.min && v <= c.max);
        } break;
        case Criterion::Kind::CounterDeltaSinceActivationAtLeast:
            result = (counterDeltaSinceActivation_(c.key, activatedAt) >= c.target);
            break;
        case Criterion::Kind::CounterInWindowAtLeast:
            result = (counterDeltaInWindow_(c.key, c.windowSecs) >= c.target);
            break;
        case Criterion::Kind::EventCountInWindowAtLeast:
            result = (eventCountInWindow_(makeKey_("ev:", c.key), c.windowSecs) >= c.target);
            break;
        case Criterion::Kind::NoEventInWindow:
            result = noEventInWindow_(makeKey_("ev:", c.key), c.windowSecs);
            break;
        case Criterion::Kind::FlagEquals:
            result = ((state_.flags.count(c.key) != 0) == c.expectedFlag);
            break;
        case Criterion::Kind::TimeElapsed:
            result = ((state_.elapsedSeconds - activatedAt) >= c.seconds);
            break;
        case Criterion::Kind::Predicate:
            result = (c.predicate ? c.predicate(state_) : false);
            break;
    }
    return c.negate ? !result : result;
}

float ObjectiveTracker::criterionProgress_(const Criterion& c, double activatedAt) const {
    auto clamp01 = [](double x) { return float(x < 0 ? 0 : (x > 1 ? 1 : x)); };
    switch (c.kind) {
        case Criterion::Kind::CounterAtLeast: {
            double v = double(readCounter_(c.key));
            if (c.target <= 0) return 1.f;
            return clamp01(v / double(c.target));
        }
        case Criterion::Kind::CounterAtMost: {
            double v = double(readCounter_(c.key));
            if (c.target <= 0) return 0.f; // <=0 is trivial but visualize as 0->1 as v decreases
            return clamp01(1.0 - (v / double(std::max<std::int64_t>(1, c.target))));
        }
        case Criterion::Kind::CounterEqual: {
            double v = double(readCounter_(c.key));
            return (v == double(c.target)) ? 1.f : 0.f;
        }
        case Criterion::Kind::CounterRange: {
            const double v = double(readCounter_(c.key));
            if (c.max <= c.min) return v >= c.min ? 1.f : 0.f;
            return clamp01((v - c.min) / double(c.max - c.min));
        }
        case Criterion::Kind::CounterDeltaSinceActivationAtLeast: {
            const double v = double(counterDeltaSinceActivation_(c.key, activatedAt));
            if (c.target <= 0) return 1.f;
            return clamp01(v / double(c.target));
        }
        case Criterion::Kind::CounterInWindowAtLeast: {
            const double v = double(counterDeltaInWindow_(c.key, c.windowSecs));
            if (c.target <= 0) return 1.f;
            return clamp01(v / double(c.target));
        }
        case Criterion::Kind::EventCountInWindowAtLeast: {
            const double v = double(eventCountInWindow_(makeKey_("ev:", c.key), c.windowSecs));
            if (c.target <= 0) return 1.f;
            return clamp01(v / double(c.target));
        }
        case Criterion::Kind::NoEventInWindow: {
            const bool ok = noEventInWindow_(makeKey_("ev:", c.key), c.windowSecs);
            const double last = timeSinceLastEvent_(makeKey_("ev:", c.key));
            const double p = (c.windowSecs <= 0.0)
                                 ? (ok ? 1.0 : 0.0)
                                 : std::clamp(1.0 - (last / c.windowSecs), 0.0, 1.0);
            return clamp01(ok ? 1.0 : p);
        }
        case Criterion::Kind::FlagEquals: {
            const bool ok = (state_.flags.count(c.key) != 0) == c.expectedFlag;
            return ok ? 1.f : 0.f;
        }
        case Criterion::Kind::TimeElapsed: {
            if (c.seconds <= 0.0) return 1.f;
            const double since = state_.elapsedSeconds - activatedAt;
            return clamp01(since / c.seconds);
        }
        case Criterion::Kind::Predicate: {
            const bool ok = (c.predicate ? c.predicate(state_) : false);
            return ok ? 1.f : 0.f;
        }
    }
    return 0.f;
}

float ObjectiveTracker::objectiveProgress_(const Objective& o) const {
    // Weighted mean of own criteria + sub-criteria.
    double sumW = 0.0, sum = 0.0;

    if (!o.criteria.empty()) {
        double w = 0.0, s = 0.0;
        for (const auto& c : o.criteria) {
            const double cw = (c.weight > 0.0 ? c.weight : 1.0);
            const double cp = criterionProgress_(c, o.activatedAt);
            w += cw;
            s += cw * cp;
        }
        sumW += w;
        sum += s;
    }

    if (!o.subs.empty()) {
        double w = 0.0, s = 0.0;
        for (const auto& so : o.subs) {
            double lw = 0.0, ls = 0.0;
            for (const auto& c : so.criteria) {
                const double cw = (c.weight > 0.0 ? c.weight : 1.0);
                const double cp = criterionProgress_(c, so.activatedAt);
                lw += cw;
                ls += cw * cp;
            }
            const double p = (lw > 0.0 ? ls / lw : 1.0);
            w += 1.0;
            s += p; // equal weight for subs
        }
        sumW += w;
        sum += s;
    }

    return float(sumW > 0.0 ? (sum / sumW) : 1.0);
}

// -------- Telemetry-backed helpers --------

std::int64_t ObjectiveTracker::counterDeltaInWindow_(const std::string& ctrName, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
    if (windowSecs <= 0.0) return readCounter_(ctrName);
    const double t0 = state_.elapsedSeconds - windowSecs;
    const std::string full = makeKey_("ctr:", ctrName);
    std::int64_t sum = 0;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->t < t0) break;
        if (it->name == full) sum += it->delta;
    }
    return sum;
#else
    (void)windowSecs;
    return readCounter_(ctrName);
#endif
}

std::int64_t ObjectiveTracker::counterDeltaSinceActivation_(const std::string& ctrName, double activatedAt) const {
#if SLICE_OT_ENABLE_TELEMETRY
    const double t0 = activatedAt;
    const std::string full = makeKey_("ctr:", ctrName);
    std::int64_t sum = 0;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->t < t0) break;
        if (it->name == full) sum += it->delta;
    }
    return sum;
#else
    // Fallback to snapshot if available
    auto oit = counterSnapshots_.find(index_);
    if (oit != counterSnapshots_.end()) {
        auto cit = oit->second.find(ctrName);
        const std::int64_t base = (cit == oit->second.end()) ? 0 : cit->second;
        return readCounter_(ctrName) - base;
    }
    return readCounter_(ctrName); // best-effort
#endif
}

std::int64_t ObjectiveTracker::eventCountInWindow_(const std::string& fullEvent, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
    if (windowSecs <= 0.0) return 0;
    const double t0 = state_.elapsedSeconds - windowSecs;
    std::int64_t cnt = 0;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->t < t0) break;
        if (it->name == fullEvent) cnt += it->delta;
    }
    return cnt;
#else
    (void)fullEvent;
    (void)windowSecs;
    return 0;
#endif
}

bool ObjectiveTracker::noEventInWindow_(const std::string& fullEvent, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
    if (windowSecs <= 0.0) return true;
    const double t0 = state_.elapsedSeconds - windowSecs;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->t < t0) break;
        if (it->name == fullEvent) return false;
    }
    return true;
#else
    (void)fullEvent;
    (void)windowSecs;
    return false;
#endif
}

double ObjectiveTracker::timeSinceLastEvent_(const std::string& fullEvent) const {
#if SLICE_OT_ENABLE_TELEMETRY
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->name == fullEvent) return std::max(0.0, state_.elapsedSeconds - it->t);
    }
    return std::numeric_limits<double>::infinity();
#else
    (void)fullEvent;
    return std::numeric_limits<double>::infinity();
#endif
}

// -------- Counters & logging --------

void ObjectiveTracker::notifyCounterImpl_(std::string_view name, std::int64_t delta) {
    auto& v = state_.counters[std::string(name)];
    v += delta;
    logEvent_(makeKey_("ctr:", name), delta, v);
    advance_();
}

void ObjectiveTracker::notifyEventUnlocked_(std::string_view eventName, std::int64_t count) {
    logEvent_(makeKey_("ev:", eventName), count, count);
}

std::int64_t ObjectiveTracker::readCounter_(const std::string& name) const {
    auto it = state_.counters.find(name);
    return (it == state_.counters.end()) ? 0 : it->second;
}

void ObjectiveTracker::logEvent_(std::string name, std::int64_t delta, std::int64_t value) {
#if SLICE_OT_ENABLE_TELEMETRY
    if constexpr (SLICE_OT_LOG_CAPACITY > 0) {
        if (log_.size() >= SLICE_OT_LOG_CAPACITY) log_.pop_front();
        log_.push_back(EventRecord{ state_.elapsedSeconds, std::move(name), delta, value });
    }
#else
    (void)name;
    (void)delta;
    (void)value;
#endif
}

// -------- Snapshots (used if telemetry disabled) --------

void ObjectiveTracker::captureSnapshotsFor_(std::size_t idx) {
#if !SLICE_OT_ENABLE_TELEMETRY
    if (idx >= objectives_.size()) return;
    std::unordered_map<std::string, std::int64_t> snap;
    const auto& o = objectives_[idx];
    auto capture = [&](const Criterion& c) {
        if (c.kind == Criterion::Kind::CounterDeltaSinceActivationAtLeast || c.kind == Criterion::Kind::CounterInWindowAtLeast ||
            c.kind == Criterion::Kind::CounterAtLeast || c.kind == Criterion::Kind::CounterAtMost || c.kind == Criterion::Kind::CounterEqual ||
            c.kind == Criterion::Kind::CounterRange) {
            snap[c.key] = readCounter_(c.key);
        }
    };
    for (const auto& c : o.criteria) capture(c);
    for (const auto& so : o.subs)
        for (const auto& c : so.criteria) capture(c);
    counterSnapshots_[idx] = std::move(snap);
#else
    (void)idx;
#endif
}

// -------- Formatting helpers --------

std::string ObjectiveTracker::mmss_(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int s = int(seconds + 0.5);
    int m = s / 60;
    s %= 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << m << ":" << std::setw(2) << s;
    return oss.str();
}

std::string ObjectiveTracker::percent_(double p01) {
    if (p01 < 0.0)
        p01 = 0.0;
    else if (p01 > 1.0)
        p01 = 1.0;
    const int pct = int(p01 * 100.0 + 0.5);
    return std::to_string(pct) + "%";
}

std::string ObjectiveTracker::progressBar_(double p01, int width) {
    if (width <= 0) return "";
    if (p01 < 0) p01 = 0;
    else if (p01 > 1) p01 = 1;
    int full = int(p01 * width + 0.5);
    std::string s;
    s.reserve(size_t(width) + 2);
    s.push_back('[');
    for (int i = 0; i < width; ++i) s.push_back(i < full ? '#' : '-');
    s.push_back(']');
    return s;
}

std::string ObjectiveTracker::statusTag_(Status st) {
    switch (st) {
        case Status::Locked: return " [LOCKED]";
        case Status::Active: return " [ACTIVE]";
        case Status::Completed: return " [DONE]";
        case Status::Failed: return " [FAILED]";
        case Status::Skipped: return " [SKIPPED]";
    }
    return {};
}

std::string ObjectiveTracker::hudLabel_(const Criterion& c) const {
    if (!c.label.empty()) return loc_(c.label);
    switch (c.kind) {
        case Criterion::Kind::CounterAtLeast: return c.key + " >= " + std::to_string(c.target);
        case Criterion::Kind::CounterAtMost: return c.key + " <= " + std::to_string(c.target);
        case Criterion::Kind::CounterEqual: return c.key + " == " + std::to_string(c.target);
        case Criterion::Kind::CounterRange:
            return c.key + " in [" + std::to_string(c.min) + "," + std::to_string(c.max) + "]";
        case Criterion::Kind::CounterDeltaSinceActivationAtLeast:
            return c.key + " +" + std::to_string(c.target) + " since start";
        case Criterion::Kind::CounterInWindowAtLeast:
            return c.key + " +" + std::to_string(c.target) + " in last " + std::to_string(int(c.windowSecs)) + "s";
        case Criterion::Kind::EventCountInWindowAtLeast:
            return c.key + " x" + std::to_string(c.target) + " in " + std::to_string(int(c.windowSecs)) + "s";
        case Criterion::Kind::NoEventInWindow:
            return "No " + c.key + " in " + std::to_string(int(c.windowSecs)) + "s";
        case Criterion::Kind::FlagEquals: return c.key + (c.expectedFlag ? " ON" : " OFF");
        case Criterion::Kind::TimeElapsed: return "Time " + std::to_string(int(c.seconds)) + "s";
        case Criterion::Kind::Predicate: return "Predicate";
    }
    return {};
}

std::string ObjectiveTracker::extraDetail_(const Criterion& c, double activatedAt) const {
    std::ostringstream oss;
    if (c.kind == Criterion::Kind::CounterAtLeast) {
        oss << "  (" << readCounter_(c.key) << " / " << c.target << ")";
    } else if (c.kind == Criterion::Kind::CounterAtMost) {
        oss << "  (" << readCounter_(c.key) << " \xE2\x89\xA4 " << c.target << ")";
    } else if (c.kind == Criterion::Kind::CounterEqual) {
        oss << "  (" << readCounter_(c.key) << ")";
    } else if (c.kind == Criterion::Kind::CounterRange) {
        oss << "  (" << readCounter_(c.key) << " in [" << c.min << "," << c.max << "])";
    } else if (c.kind == Criterion::Kind::CounterDeltaSinceActivationAtLeast) {
        oss << "  (" << counterDeltaSinceActivation_(c.key, activatedAt) << " / " << c.target << ")";
    } else if (c.kind == Criterion::Kind::CounterInWindowAtLeast) {
        oss << "  (" << counterDeltaInWindow_(c.key, c.windowSecs) << " / " << c.target << " in " << int(c.windowSecs) << "s)";
    } else if (c.kind == Criterion::Kind::EventCountInWindowAtLeast) {
        oss << "  (" << eventCountInWindow_(makeKey_("ev:", c.key), c.windowSecs) << " / " << c.target << " in " << int(c.windowSecs)
            << "s)";
    } else if (c.kind == Criterion::Kind::TimeElapsed) {
        const double since = state_.elapsedSeconds - activatedAt;
        oss << "  (" << mmss_(since) << " / " << mmss_(c.seconds) << ")";
    }
    return oss.str();
}

// -------- Status / progress firing --------

void ObjectiveTracker::fireProgressWithWatchers_(const Objective& o, float prog) {
    // global progress callback
    if (onProgress_) onProgress_(o, prog, state_);

    // threshold watchers (fire once when crossing upwards)
    const std::size_t idx = findIndexUnsafe_(o.id);
    if (idx < lastProgress_.size()) {
        const float prev = lastProgress_[idx];
        lastProgress_[idx] = prog;
        for (auto& w : watchers_) {
            if (w.objectiveIndex == idx && !w.fired && prev < w.threshold && prog >= w.threshold) {
                w.fired = true;
                if (w.cb) w.cb(o, w.threshold, state_);
            }
        }
    }
}

void ObjectiveTracker::fireStatus_(const Objective& o, Status old, Status now) {
    if (onStatus_) onStatus_(o, old, now, state_);
}

void ObjectiveTracker::setFail_(Objective& o, std::string reason) {
    const Status old = o.status;
    o.status = Status::Failed;
    o.lastFailReason = std::move(reason);
    totalScore_ -= o.scorePenaltyOnFail;
    if (o.onFail) o.onFail(state_);
    fireStatus_(o, old, o.status);
}

std::size_t ObjectiveTracker::findIndexUnsafe_(const std::string& id) const {
    for (std::size_t i = 0; i < objectives_.size(); ++i) {
        if (objectives_[i].id == id) return i;
    }
    return objectives_.size();
}

// -------- Localization --------

std::string ObjectiveTracker::loc_(const std::string& s) const {
    if (!localize_) return s;
    if (!s.empty() && s[0] == locPrefix_) {
        return localize_(std::string_view(s).substr(1));
    }
    return s;
}

} // namespace slice
