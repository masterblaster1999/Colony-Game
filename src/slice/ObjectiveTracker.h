// src/slice/ObjectiveTracker.h
#pragma once
/*
    ObjectiveTracker.h — Objective & Achievement System for Colony-Game
    -------------------------------------------------------------------
    • Header-only (C++17). No hard deps; optional JSON via nlohmann/json.
    • Sequential objectives with hierarchical sub-objectives (ALL / ANY).
    • Criteria:
        - CounterAtLeast / AtMost / Equal / Range
        - CounterDeltaSinceActivationAtLeast
        - EventCountInWindowAtLeast
        - CounterInWindowAtLeast
        - NoEventInWindow
        - FlagEquals
        - TimeElapsed
        - Predicate
        (+) Each criterion can be negated (invert result) and weighted.
    • Generic counters, flags, and named events API.
    • Telemetry ring buffer powers time-window queries and deltas.
    • Pause/resume & time-scale (for survival-style loops).
    • Score, thresholds (progress callbacks), checkpoints, repeatables.
    • Branching: nextOnComplete / nextOnFail (by objective id).
    • Localization hook: provide a function to resolve $tokens to text.
    • Save/Load: compact text format (versioned) + optional JSON.

    Integration:
      - Call update(dt) each frame.
      - From systems: notifyStructureBuilt(), notifyItemCrafted(), etc.,
        or generic notifyCounter()/setFlag()/notifyEvent().
      - Draw HUD lines via hudLines() until your UI is ready.

    License: follow your repo’s LICENSE (MIT/Apache-2.0 advisable).
*/

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable: 26495) // POD member may be uninitialized (false positive in some configs)
#endif

// =========================== Compile-time configuration =======================

#ifndef SLICE_OT_VERSION_MAJOR
  #define SLICE_OT_VERSION_MAJOR 3
  #define SLICE_OT_VERSION_MINOR 0
#endif

#ifndef SLICE_OT_THREAD_SAFE
  // Set to 1 to guard public mutating APIs with a mutex.
  #define SLICE_OT_THREAD_SAFE 0
#endif

#ifndef SLICE_OT_ENABLE_TELEMETRY
  // Ring buffer of recent events (powers time-window criteria).
  #define SLICE_OT_ENABLE_TELEMETRY 1
#endif

#ifndef SLICE_OT_LOG_CAPACITY
  // Telemetry ring buffer capacity (0 to disable at compile time).
  #define SLICE_OT_LOG_CAPACITY 640
#endif

#ifndef SLICE_OT_ENABLE_HASH
  // Compile-time FNV-1a 64-bit hashing for ids (optional).
  #define SLICE_OT_ENABLE_HASH 1
#endif

#ifndef SLICE_OT_ASSERT
  #define SLICE_OT_ASSERT(x) assert(x)
#endif

#if SLICE_OT_THREAD_SAFE
  #include <mutex>
  #define SLICE_OT_LOCK_GUARD std::lock_guard<std::mutex> _slice_ot_lock(mtx_)
#else
  #define SLICE_OT_LOCK_GUARD do{}while(0)
#endif

// Optional JSON (single header). Enable before including this file:
//   #define SLICE_OT_USE_NLOHMANN_JSON 1
//   #include <nlohmann/json.hpp>
#if SLICE_OT_USE_NLOHMANN_JSON
  #include <nlohmann/json.hpp>
#endif

namespace slice {

// ================================ Utilities ==================================

#if SLICE_OT_ENABLE_HASH
// constexpr FNV-1a 64-bit (ASCII/UTF-8). Non-cryptographic. :contentReference[oaicite:4]{index=4}
constexpr std::uint64_t fnv1a64(const char* s, std::uint64_t h = 14695981039346656037ull) {
    return (*s == 0) ? h : fnv1a64(s + 1, (h ^ static_cast<unsigned char>(*s)) * 1099511628211ull);
}
struct HashedId {
    std::uint64_t value = 0;
    constexpr HashedId() = default;
    constexpr explicit HashedId(const char* str) : value(fnv1a64(str)) {}
    constexpr bool operator==(const HashedId& o) const { return value == o.value; }
    constexpr bool operator!=(const HashedId& o) const { return value != o.value; }
};
#endif

// ================================ Core enums =================================

enum class Status { Locked, Active, Completed, Failed, Skipped };
enum class Logic  { All, Any };

// ================================ Slice state =================================

struct SliceState {
    // Built-in counters relevant to colony loop:
    int    structuresBuilt = 0;
    int    itemsCrafted    = 0;
    int    colonistsAlive  = 3;
    bool   lost            = false;

    // Time management:
    double elapsedSeconds  = 0.0;  // accumulate via update(dt)
    double timeScale       = 1.0;  // 1.0 == real time
    bool   paused          = false;

    // Extensibility:
    std::unordered_map<std::string, std::int64_t> counters;
    std::unordered_set<std::string>               flags;

    void clear() {
        structuresBuilt = 0;
        itemsCrafted    = 0;
        colonistsAlive  = 3;
        lost            = false;
        elapsedSeconds  = 0.0;
        timeScale       = 1.0;
        paused          = false;
        counters.clear();
        flags.clear();
    }
};

// ================================ Criteria ====================================

struct Criterion {
    enum class Kind {
        CounterAtLeast,                 // counter >= target
        CounterAtMost,                  // counter <= target
        CounterEqual,                   // counter == target
        CounterRange,                   // min <= counter <= max
        CounterDeltaSinceActivationAtLeast, // (counter(t)-counter(activate)) >= target
        CounterInWindowAtLeast,         // sum of counter deltas over last windowSecs >= target
        EventCountInWindowAtLeast,      // count(eventName) in last windowSecs >= target
        NoEventInWindow,                // no eventName occurred in last windowSecs
        FlagEquals,                     // flag presence equals expected
        TimeElapsed,                    // (now - activatedAt) >= seconds
        Predicate                       // custom predicate(state) -> bool
    };

    Kind        kind = Kind::Predicate;
    bool        negate = false;        // invert result if true
    std::string key;                   // counter/flag/event name
    std::string label;                 // HUD label (optional)
    double      weight = 1.0;          // average weighting

    // Numeric thresholds:
    std::int64_t target = 0;           // used by many counter/event kinds
    std::int64_t min = 0, max = 0;     // range

    // Time parameters:
    double      seconds = 0.0;         // TimeElapsed
    double      windowSecs = 0.0;      // window-based criteria

    // Flag:
    bool        expectedFlag = true;

    // Custom:
    std::function<bool(const SliceState&)> predicate;

    // Visibility:
    bool hiddenInHud = false;

    // ---- Factories ----
    static Criterion counterAtLeast(std::string name, std::int64_t atLeast,
                                    std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterAtLeast; c.key=std::move(name); c.target=atLeast;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion counterAtMost(std::string name, std::int64_t atMost,
                                   std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterAtMost; c.key=std::move(name); c.target=atMost;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion counterEqual(std::string name, std::int64_t eq,
                                  std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterEqual; c.key=std::move(name); c.target=eq;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion counterRange(std::string name, std::int64_t mi, std::int64_t ma,
                                  std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterRange; c.key=std::move(name); c.min=mi; c.max=ma;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion counterDeltaSinceActivationAtLeast(std::string name, std::int64_t atLeast,
                                                        std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterDeltaSinceActivationAtLeast; c.key=std::move(name); c.target=atLeast;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion counterInWindowAtLeast(std::string name, std::int64_t atLeast, double windowSeconds,
                                            std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::CounterInWindowAtLeast; c.key=std::move(name); c.target=atLeast; c.windowSecs=windowSeconds;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion eventCountInWindowAtLeast(std::string eventName, std::int64_t atLeast, double windowSeconds,
                                               std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::EventCountInWindowAtLeast; c.key=std::move(eventName); c.target=atLeast; c.windowSecs=windowSeconds;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion noEventInWindow(std::string eventName, double windowSeconds,
                                     std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::NoEventInWindow; c.key=std::move(eventName); c.windowSecs=windowSeconds;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion flagEquals(std::string name, bool expected = true,
                                std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::FlagEquals; c.key=std::move(name); c.expectedFlag=expected;
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion timeElapsed(double secs, std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::TimeElapsed; c.seconds=secs; c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
    static Criterion predicateFn(std::function<bool(const SliceState&)> pred,
                                 std::string hud = {}, double w = 1.0, bool neg=false) {
        Criterion c; c.kind=Kind::Predicate; c.predicate=std::move(pred);
        c.label=std::move(hud); c.weight=w; c.negate=neg; return c;
    }
};

// ============================== Sub-objectives ================================

struct SubObjective {
    std::string id;
    std::string title;
    Logic       logic = Logic::All;
    std::vector<Criterion> criteria;

    // Callbacks:
    std::function<void(SliceState&)> onActivate;
    std::function<void(SliceState&)> onComplete;
    std::function<void(SliceState&)> onFail;

    // Runtime:
    Status status = Status::Locked;
    double activatedAt = 0.0;

    struct Builder {
        SubObjective so;
        explicit Builder(std::string id_) { so.id = std::move(id_); }
        Builder& title(std::string t){ so.title = std::move(t); return *this; }
        Builder& allOf(std::vector<Criterion> v){ so.logic = Logic::All; so.criteria = std::move(v); return *this; }
        Builder& anyOf(std::vector<Criterion> v){ so.logic = Logic::Any; so.criteria = std::move(v); return *this; }
        Builder& onActivateFn(std::function<void(SliceState&)> f){ so.onActivate = std::move(f); return *this; }
        Builder& onCompleteFn(std::function<void(SliceState&)> f){ so.onComplete = std::move(f); return *this; }
        Builder& onFailFn(std::function<void(SliceState&)> f){ so.onFail = std::move(f); return *this; }
        SubObjective build(){ return std::move(so); }
    };
};

// ================================ Objectives ==================================

struct Objective {
    std::string id;
    std::string title;
    std::string description;

    // Own criteria:
    Logic logic = Logic::All;
    std::vector<Criterion> criteria;

    // Sub-objectives:
    Logic subLogic = Logic::All;
    int   minSubsToComplete = -1; // -1 => all (for ALL); for ANY: >=1 by default
    std::vector<SubObjective> subs;

    // Fail rules:
    std::optional<int>    minColonistsAlive;
    std::optional<double> timeoutSeconds;
    bool                  failIfLost = true;

    // Scoring & progression:
    int    scoreReward = 0;
    int    scorePenaltyOnFail = 0;
    double weight = 1.0;
    bool   checkpoint = false;
    int    repeatCountTarget = 1;   // times the objective must complete
    int    repeatCountProgress = 0; // runtime

    // Branching (by objective id; falls back to linear sequence if empty):
    std::string nextOnCompleteId;
    std::string nextOnFailId;

    // Enable/disable
    bool enabled = true;

    // Callbacks:
    std::function<void(SliceState&)> onActivate;
    std::function<void(SliceState&)> onComplete;
    std::function<void(SliceState&)> onFail;

    // Runtime:
    Status status = Status::Locked;
    double activatedAt = 0.0;
    double completedAt = 0.0;
    std::string lastFailReason;

    struct Builder {
        Objective o;
        explicit Builder(std::string id_) { o.id = std::move(id_); }
        Builder& title(std::string t){ o.title = std::move(t); return *this; }
        Builder& desc(std::string d){ o.description = std::move(d); return *this; }
        Builder& allOf(std::vector<Criterion> v){ o.logic = Logic::All; o.criteria = std::move(v); return *this; }
        Builder& anyOf(std::vector<Criterion> v){ o.logic = Logic::Any; o.criteria = std::move(v); return *this; }
        Builder& subAll(std::vector<SubObjective> v){ o.subLogic = Logic::All; o.subs = std::move(v); return *this; }
        Builder& subAny(std::vector<SubObjective> v, int minCount = 1){ o.subLogic = Logic::Any; o.subs = std::move(v); o.minSubsToComplete = std::max(1, minCount); return *this; }
        Builder& minColonists(int n){ o.minColonistsAlive = n; return *this; }
        Builder& timeout(double secs){ o.timeoutSeconds = secs; return *this; }
        Builder& reward(int s){ o.scoreReward = s; return *this; }
        Builder& penaltyOnFail(int s){ o.scorePenaltyOnFail = s; return *this; }
        Builder& weight(double w){ o.weight = w; return *this; }
        Builder& markCheckpoint(bool v=true){ o.checkpoint = v; return *this; }
        Builder& repeatable(int times){ o.repeatCountTarget = std::max(1, times); return *this; }
        Builder& nextOnComplete(std::string id){ o.nextOnCompleteId = std::move(id); return *this; }
        Builder& nextOnFail(std::string id){ o.nextOnFailId = std::move(id); return *this; }
        Builder& onActivateFn(std::function<void(SliceState&)> f){ o.onActivate = std::move(f); return *this; }
        Builder& onCompleteFn(std::function<void(SliceState&)> f){ o.onComplete = std::move(f); return *this; }
        Builder& onFailFn(std::function<void(SliceState&)> f){ o.onFail = std::move(f); return *this; }
        Objective build(){ return std::move(o); }
    };
};

// ================================ Tracker =====================================

class ObjectiveTracker {
public:
    // Telemetry record (counters/flags/events). Window criteria read from here.
    struct EventRecord {
        double      t = 0.0;           // slice time (seconds)
        std::string name;              // "ctr:<k>", "flg:<k>", "ev:<k>"
        std::int64_t delta = 0;        // +N / -N for counters; ±1 for flags; +count for events
        std::int64_t value = 0;        // resulting absolute value (flags: 0/1)
    };

    // Callbacks
    using ProgressCallback = std::function<void(const Objective&, float /*0..1*/, const SliceState&)>;
    using StatusCallback   = std::function<void(const Objective&, Status /*old*/, Status /*now*/, const SliceState&)>;
    using ThresholdCallback= std::function<void(const Objective&, double /*threshold*/, const SliceState&)>;
    using LocalizeFn       = std::function<std::string(std::string_view token)>;

    ObjectiveTracker() = default;

    // ---------------- Lifecycle ----------------
    void reset() {
        SLICE_OT_LOCK_GUARD;
        state_.clear();
        totalScore_ = 0;
        index_ = static_cast<std::size_t>(-1);
        lastCheckpoint_.reset();
        lastProgress_.assign(objectives_.size(), 0.0f);
        counterSnapshots_.clear();
        for (auto& o : objectives_) {
            o.status = Status::Locked; o.activatedAt = 0.0; o.completedAt = 0.0; o.lastFailReason.clear(); o.repeatCountProgress = 0;
            for (auto& so : o.subs) { so.status = Status::Locked; so.activatedAt = 0.0; }
        }
#if SLICE_OT_ENABLE_TELEMETRY
        log_.clear();
#endif
    }

    void start() {
        SLICE_OT_LOCK_GUARD;
        if (objectives_.empty()) return;
        // Find first enabled objective
        index_ = firstEnabledIndexFrom_(0);
        if (index_ < objectives_.size()) { activateObjective_(index_); }
    }

    void pause(bool v)                  { SLICE_OT_LOCK_GUARD; state_.paused = v; }
    void resume()                       { pause(false); }
    void setTimeScale(double s)         { SLICE_OT_LOCK_GUARD; state_.timeScale = (s>0? s:0); }
    void setLocalizer(LocalizeFn fn)    { SLICE_OT_LOCK_GUARD; localize_ = std::move(fn); }
    void setHudTokenPrefix(char ch)     { SLICE_OT_LOCK_GUARD; locPrefix_ = ch; }

    // Call once per frame with *unscaled* dt (seconds).
    void update(double dtSeconds) {
        SLICE_OT_LOCK_GUARD;
        if (isComplete() || isFailed()) return;
        if (!state_.paused) state_.elapsedSeconds += dtSeconds * state_.timeScale;
        advance_();
    }

    // ---------------- Build & query ----------------
    Objective& add(Objective o) {
        SLICE_OT_LOCK_GUARD;
        objectives_.push_back(std::move(o));
        lastProgress_.push_back(0.0f);
        return objectives_.back();
    }
    Objective& add(Objective::Builder b) { return add(b.build()); }

    bool isComplete() const { return objectives_.empty() || index_ >= objectives_.size(); }
    bool isFailed()   const { return !isComplete() && objectives_[index_].status == Status::Failed; }
    std::size_t objectiveCount() const { return objectives_.size(); }
    const Objective* current() const { return isComplete()? nullptr : &objectives_[index_]; }
    std::size_t currentIndex() const { return index_; }

    std::optional<std::size_t> findById(std::string_view id) const {
        for (std::size_t i=0;i<objectives_.size();++i) if (objectives_[i].id==id) return i; return std::nullopt;
    }

    // Enable/disable + branching helpers
    bool setEnabledById(std::string_view id, bool enabled) {
        SLICE_OT_LOCK_GUARD; auto idx=findById(id); if(!idx) return false; objectives_[*idx].enabled=enabled; return true;
    }

    bool skipCurrent() {
        SLICE_OT_LOCK_GUARD;
        if (isComplete()) return false;
        auto& cur = objectives_[index_];
        const Status old = cur.status; cur.status = Status::Skipped;
        fireStatus_(cur, old, cur.status);
        moveToNext_(/*branchFrom*/std::nullopt, /*onFail*/false);
        return true;
    }
    bool failCurrent(std::string reason = "debug-fail") {
        SLICE_OT_LOCK_GUARD;
        if (isComplete()) return false;
        auto& cur = objectives_[index_];
        if (cur.status != Status::Active) return false;
        setFail_(cur, std::move(reason));
        moveToNext_(/*branchFrom*/index_, /*onFail*/true);
        return true;
    }

    // Jump to objective by id (activates it).
    bool setCurrentById(std::string_view id) {
        SLICE_OT_LOCK_GUARD;
        auto idx = findById(id); if (!idx) return false;
        index_ = *idx;
        activateObjective_(index_);
        return true;
    }

    // ---------------- Event API ----------------
    // Built-ins:
    void notifyStructureBuilt(int count = 1) { SLICE_OT_LOCK_GUARD; state_.structuresBuilt += count; notifyCounterImpl_("structures.built", count); }
    void notifyItemCrafted(int count = 1)    { SLICE_OT_LOCK_GUARD; state_.itemsCrafted    += count; notifyCounterImpl_("items.crafted", count); }
    void notifyColonistSpawned(int count = 1){ SLICE_OT_LOCK_GUARD; state_.colonistsAlive  += count; }
    void notifyColonistDied(int count = 1)   { SLICE_OT_LOCK_GUARD; state_.colonistsAlive  -= count; if (state_.colonistsAlive <= 0) state_.lost = true; notifyEventUnlocked_("colonist.death", 1); advance_(); }

    // Generic counters/flags/events:
    void notifyCounter(std::string_view name, std::int64_t delta = 1) { SLICE_OT_LOCK_GUARD; notifyCounterImpl_(name, delta); }
    void setCounter(std::string_view name, std::int64_t value) {
        SLICE_OT_LOCK_GUARD;
        auto& v = state_.counters[std::string(name)];
        auto delta = value - v; v = value;
        logEvent_("ctr:"+std::string(name), delta, v);
        advance_();
    }
    std::int64_t getCounter(std::string_view name) const {
        auto it = state_.counters.find(std::string(name));
        return (it==state_.counters.end()) ? 0 : it->second;
    }
    void setFlag(std::string_view name, bool value = true) {
        SLICE_OT_LOCK_GUARD;
        const std::string k(name);
        bool changed=false;
        if (value) changed = state_.flags.insert(k).second; else changed = state_.flags.erase(k)>0;
        if (changed) logEvent_("flg:"+k, value ? 1 : -1, value ? 1 : 0);
        advance_();
    }
    bool getFlag(std::string_view name) const { return state_.flags.count(std::string(name)) != 0; }

    // Named event (not a counter or flag). Used by window/no-event criteria.
    void notifyEvent(std::string_view eventName, std::int64_t count = 1) {
        SLICE_OT_LOCK_GUARD; notifyEventUnlocked_(eventName, count); advance_();
    }

    // ---------------- HUD / Progress ----------------
    struct HudOptions {
        int  barWidth = 18;
        bool showCounters = true;
        bool showFlags    = false;
        bool showTimer    = true;
        bool showSubObjectives = true;
        bool showCompletedCheck = true;
    };

    std::vector<std::string> hudLines(const HudOptions& opt = {}) const {
        std::vector<std::string> out;
        if (isComplete()) { out.emplace_back("Vertical Slice: COMPLETE (Score: " + std::to_string(totalScore_) + ")"); return out; }
        if (isFailed())   { const auto* c = current(); out.emplace_back("Vertical Slice: FAILED — " + (c ? c->lastFailReason : "unknown")); return out; }

        const Objective* cur = current(); SLICE_OT_ASSERT(cur);
        out.emplace_back("Objective: " + loc_(cur->title) + (opt.showCompletedCheck ? "  [ ]" : ""));
        if (!cur->description.empty()) out.emplace_back(loc_(cur->description));

        // Own criteria
        for (const auto& c : cur->criteria) {
            if (c.hiddenInHud) continue;
            const float p = criterionProgress_(c, cur->activatedAt);
            const std::string label = hudLabel_(c);
            out.emplace_back(" - " + label + ": " + progressBar_(p, opt.barWidth) + " " + percent_(p) + extraDetail_(c, cur->activatedAt));
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
                    out.emplace_back("      • " + label + ": " + progressBar_(p, opt.barWidth) + " " + percent_(p) + extraDetail_(c, so.activatedAt));
                }
            }
        }

        if (opt.showCounters) {
            out.emplace_back("Built: " + std::to_string(state_.structuresBuilt) + "   Crafted: " + std::to_string(state_.itemsCrafted));
        }
        out.emplace_back(std::string("Colonists: ") + std::to_string(state_.colonistsAlive) + (state_.lost ? " (LOST)" : ""));
        if (opt.showTimer) out.emplace_back("Survival: " + mmss_(state_.elapsedSeconds));
        return out;
    }

    // Overall 0..1 progress (weighted per objective).
    double overallProgress() const {
        if (objectives_.empty()) return 1.0;
        double sumW = 0.0, sumP = 0.0;
        for (std::size_t i=0;i<objectives_.size();++i) {
            const auto& o = objectives_[i];
            const double w = (o.weight>0.0? o.weight : 1.0);
            sumW += w;
            double p=0.0;
            switch (o.status) {
                case Status::Completed: p=1.0; break;
                case Status::Active:    p=objectiveProgress_(o); break;
                default:                p=0.0; break;
            }
            sumP += w * p;
        }
        return (sumW>0.0? sumP/sumW : 0.0);
    }

    // ---------------- Observability ----------------
    void setProgressCallback(ProgressCallback cb) { SLICE_OT_LOCK_GUARD; onProgress_ = std::move(cb); }
    void setStatusCallback(StatusCallback cb)     { SLICE_OT_LOCK_GUARD; onStatus_   = std::move(cb); }

    // Fire callback the first time an objective's progress crosses a threshold (0..1].
    bool addProgressThresholdWatcherById(std::string_view id, double threshold, ThresholdCallback cb) {
        SLICE_OT_LOCK_GUARD;
        auto idx = findById(id); if(!idx) return false;
        threshold = std::clamp(threshold, 0.0, 1.0);
        watchers_.push_back({*idx, threshold, false, std::move(cb)});
        return true;
    }

    // ---------------- Checkpoints ----------------
    std::optional<std::size_t> lastCheckpointIndex() const { return lastCheckpoint_; }
    bool restoreToLastCheckpoint() {
        SLICE_OT_LOCK_GUARD;
        if (!lastCheckpoint_ || *lastCheckpoint_ >= objectives_.size()) return false;
        for (std::size_t i = *lastCheckpoint_; i < objectives_.size(); ++i) {
            auto& o = objectives_[i];
            o.status = Status::Locked; o.activatedAt = 0.0; o.completedAt = 0.0; o.lastFailReason.clear(); o.repeatCountProgress=0;
            for (auto& so : o.subs) { so.status = Status::Locked; so.activatedAt = 0.0; }
        }
        index_ = *lastCheckpoint_;
        activateObjective_(index_);
        return true;
    }

    // ---------------- Save / Load ----------------
    // Compact, line-based, versioned format.
    std::string serialize() const {
        std::ostringstream oss;
        oss << "OT3 " << SLICE_OT_VERSION_MAJOR << "." << SLICE_OT_VERSION_MINOR << "\n";
        oss << "time " << state_.elapsedSeconds << " scale " << state_.timeScale << " paused " << (state_.paused?1:0) << "\n";
        oss << "sb "   << state_.structuresBuilt << " ic " << state_.itemsCrafted
            << " ca "  << state_.colonistsAlive  << " lost " << (state_.lost?1:0) << "\n";
        oss << "score " << totalScore_ << " idx " << index_ << " chk " << (lastCheckpoint_? int(*lastCheckpoint_) : -1) << "\n";

        for (const auto& kv : state_.counters) oss << "ctr " << kv.first << " " << kv.second << "\n";
        for (const auto& f  : state_.flags)    oss << "flg " << f << "\n";

        for (std::size_t i=0;i<objectives_.size();++i) {
            const auto& o = objectives_[i];
            oss << "obj " << i << " " << o.id << " " << int(o.status) << " " << o.repeatCountProgress
                << " " << o.activatedAt << " " << o.completedAt << "\n";
            for (std::size_t j=0;j<o.subs.size();++j) {
                const auto& so = o.subs[j];
                oss << "sub " << i << " " << j << " " << so.id << " " << int(so.status) << " " << so.activatedAt << "\n";
            }
        }
        return oss.str();
    }

    bool deserialize(std::string_view data) {
        SLICE_OT_LOCK_GUARD;
        std::istringstream iss(std::string(data));
        std::string tag;
        int vmaj=0, vmin=0;
        if (!(iss >> tag)) return false;

        if (tag == "OT3") { char dot; iss >> vmaj >> dot >> vmin; }
        else if (tag == "OT2" || tag == "OT1") { /* accept older, ignore extra */ }
        else return false;

        state_.clear(); totalScore_=0; index_=static_cast<std::size_t>(-1); lastCheckpoint_.reset();
        lastProgress_.assign(objectives_.size(), 0.0f);
        counterSnapshots_.clear();
        for (auto& o: objectives_) {
            o.status=Status::Locked; o.activatedAt=0.0; o.completedAt=0.0; o.lastFailReason.clear(); o.repeatCountProgress=0;
            for (auto& so: o.subs){ so.status=Status::Locked; so.activatedAt=0.0; }
        }

        while (iss >> tag) {
            if (tag=="time") { iss >> state_.elapsedSeconds; }
            else if (tag=="scale") { iss >> state_.timeScale; }
            else if (tag=="paused") { int p; iss >> p; state_.paused=(p!=0); }
            else if (tag=="sb") { iss >> state_.structuresBuilt; }
            else if (tag=="ic") { iss >> state_.itemsCrafted; }
            else if (tag=="ca") { iss >> state_.colonistsAlive; }
            else if (tag=="lost") { int b; iss >> b; state_.lost=(b!=0); }
            else if (tag=="score") { iss >> totalScore_; }
            else if (tag=="idx") { std::size_t i; iss >> i; index_=i; }
            else if (tag=="chk") { int ci; iss >> ci; if (ci>=0) lastCheckpoint_=std::size_t(ci); }
            else if (tag=="ctr") { std::string k; std::int64_t v; iss >> k >> v; state_.counters[k]=v; }
            else if (tag=="flg") { std::string k; iss >> k; state_.flags.insert(k); }
            else if (tag=="obj") {
                std::size_t i; std::string id; int st; int rpt; double act, comp;
                iss >> i >> id >> st >> rpt >> act >> comp;
                if (i < objectives_.size() && objectives_[i].id == id) {
                    auto& o = objectives_[i]; o.status=Status(st); o.repeatCountProgress=rpt; o.activatedAt=act; o.completedAt=comp;
                }
            }
            else if (tag=="sub") {
                std::size_t oi, si; std::string id; int st; double act;
                iss >> oi >> si >> id >> st >> act;
                if (oi<objectives_.size() && si<objectives_[oi].subs.size() && objectives_[oi].subs[si].id==id) {
                    auto& so = objectives_[oi].subs[si]; so.status=Status(st); so.activatedAt=act;
                }
            }
        }

        if (!objectives_.empty()) {
            if (index_ >= objectives_.size()) index_ = objectives_.size();
            else if (!isComplete() && objectives_[index_].status != Status::Active) activateObjective_(index_);
        }
        return true;
    }

#if SLICE_OT_USE_NLOHMANN_JSON
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["ver"] = { {"major", SLICE_OT_VERSION_MAJOR}, {"minor", SLICE_OT_VERSION_MINOR} };
        j["time"] = { {"elapsed", state_.elapsedSeconds}, {"scale", state_.timeScale}, {"paused", state_.paused} };
        j["builtins"] = { {"structuresBuilt", state_.structuresBuilt}, {"itemsCrafted", state_.itemsCrafted}, {"colonistsAlive", state_.colonistsAlive}, {"lost", state_.lost} };
        j["score"] = totalScore_; j["index"] = index_;
        j["checkpoint"] = lastCheckpoint_.has_value() ? nlohmann::json(*lastCheckpoint_) : nlohmann::json();
        j["counters"] = state_.counters;
        j["flags"]    = std::vector<std::string>(state_.flags.begin(), state_.flags.end());
        std::vector<nlohmann::json> objs; objs.reserve(objectives_.size());
        for (const auto& o : objectives_) {
            nlohmann::json jo{
                {"id", o.id}, {"title", o.title}, {"desc", o.description},
                {"status", int(o.status)}, {"activatedAt", o.activatedAt}, {"completedAt", o.completedAt},
                {"repeatProg", o.repeatCountProgress}, {"repeatTarget", o.repeatCountTarget},
                {"scoreReward", o.scoreReward}, {"penaltyOnFail", o.scorePenaltyOnFail}
            };
            if (!o.subs.empty()) {
                std::vector<nlohmann::json> subs;
                for (const auto& so : o.subs) subs.push_back({{"id", so.id}, {"title", so.title}, {"status", int(so.status)}, {"activatedAt", so.activatedAt}});
                jo["subs"] = std::move(subs);
            }
            objs.push_back(std::move(jo));
        }
        j["objectives"] = std::move(objs);
        return j;
    }
    bool from_json(const nlohmann::json& j) {
        SLICE_OT_LOCK_GUARD;
        state_.clear(); totalScore_ = j.value("score", 0);
        index_ = j.value("index", std::size_t(0));
        if (j.contains("time")) { const auto& t=j["time"]; state_.elapsedSeconds=t.value("elapsed",0.0); state_.timeScale=t.value("scale",1.0); state_.paused=t.value("paused",false); }
        if (j.contains("builtins")) {
            const auto& b=j["builtins"];
            state_.structuresBuilt=b.value("structuresBuilt",0);
            state_.itemsCrafted=b.value("itemsCrafted",0);
            state_.colonistsAlive=b.value("colonistsAlive",3);
            state_.lost=b.value("lost",false);
        }
        state_.counters = j.value("counters", decltype(state_.counters){});
        state_.flags.clear(); if (j.contains("flags")) for (const auto& f: j["flags"]) state_.flags.insert(f.get<std::string>());
        if (j.contains("checkpoint") && !j["checkpoint"].is_null()) lastCheckpoint_ = j["checkpoint"].get<std::size_t>(); else lastCheckpoint_.reset();

        if (j.contains("objectives")) {
            auto arr=j["objectives"];
            for (std::size_t i=0;i<arr.size() && i<objectives_.size();++i) {
                auto& o=objectives_[i]; const auto& jo=arr[i];
                if (jo.value("id", o.id) == o.id) {
                    o.status=Status(jo.value("status", int(o.status)));
                    o.activatedAt=jo.value("activatedAt", 0.0);
                    o.completedAt=jo.value("completedAt", 0.0);
                    o.repeatCountProgress = jo.value("repeatProg", 0);
                    o.repeatCountTarget   = jo.value("repeatTarget", 1);
                    if (jo.contains("subs")) {
                        auto sarr=jo["subs"];
                        for (std::size_t k=0;k<sarr.size() && k<o.subs.size();++k) {
                            auto& so=o.subs[k]; const auto& js=sarr[k];
                            if (js.value("id", so.id) == so.id) {
                                so.status = Status(js.value("status", int(so.status)));
                                so.activatedAt = js.value("activatedAt", 0.0);
                            }
                        }
                    }
                }
            }
        }
        if (!objectives_.empty() && index_ < objectives_.size() && objectives_[index_].status != Status::Active) activateObjective_(index_);
        return true;
    }
#endif // JSON

    // ---------------- Pre-canned default slice ----------------
    static ObjectiveTracker MakeDefault(double surviveSeconds,
                                        int structuresToBuild,
                                        int itemsToCraft,
                                        int startingColonists)
    {
        ObjectiveTracker t;
        t.state_.colonistsAlive = startingColonists;

        t.add(Objective::Builder("build")
            .title("$EstablishColony")
            .desc("$BuildDesc")
            .allOf({ Criterion::counterAtLeast("structures.built", structuresToBuild, "$BuildStructures") })
            .reward(50).weight(1.0).markCheckpoint(true)
        );

        t.add(Objective::Builder("craft")
            .title("$EnableProduction")
            .desc("$CraftDesc")
            .allOf({ Criterion::counterAtLeast("items.crafted", itemsToCraft, "$CraftItems") })
            .reward(50).weight(1.0)
        );

        t.add(Objective::Builder("survive")
            .title("$WeatherTheNight")
            .desc("$SurviveDesc")
            .allOf({ Criterion::timeElapsed(surviveSeconds, "$SurviveTimer") })
            .subAll({
                SubObjective::Builder("no-death-60s")
                    .title("$NoDeaths60s")
                    .allOf({ Criterion::noEventInWindow("colonist.death", 60.0, "$NoRecentDeaths") })
                    .build()
            })
            .minColonists(1).reward(100).markCheckpoint(true)
        );

        t.add(Objective::Builder("endcheck")
            .title("$KeepThemAlive")
            .desc("$EndWith3Colonists")
            .allOf({ Criterion::predicateFn([](const SliceState& s){ return s.colonistsAlive >= 3; }, "$ColonistsGte3") })
            .reward(100)
        );

        t.start();
        return t;
    }

    // ---------------- Accessors ----------------
    int score() const { return totalScore_; }
    const SliceState& state() const { return state_; }
    SliceState&       state()       { return state_; }

#if SLICE_OT_ENABLE_TELEMETRY
    std::vector<EventRecord> recentEvents(std::size_t maxCount = 64) const {
        std::vector<EventRecord> v;
        const std::size_t n = std::min(maxCount, log_.size());
        v.reserve(n);
        for (std::size_t i = log_.size() - n; i < log_.size(); ++i) v.push_back(log_[i]);
        return v;
    }
#endif

// ============================= Implementation ================================
private:
    // -------- Activation & sequencing --------
    void activateObjective_(std::size_t idx) {
        SLICE_OT_ASSERT(idx < objectives_.size());
        auto& o = objectives_[idx];
        if (!o.enabled) { // skip disabled to next enabled
            moveToNext_(/*branchFrom*/std::nullopt, /*onFail*/false);
            return;
        }
        Status old = o.status;
        o.status = Status::Active;
        o.activatedAt = state_.elapsedSeconds;
        // activate subs
        for (auto& so : o.subs) { so.status = Status::Active; so.activatedAt = o.activatedAt; if (so.onActivate) so.onActivate(state_); }

        // capture snapshots for delta-since-activation (when telemetry disabled)
        captureSnapshotsFor_(idx);

        if (o.onActivate) o.onActivate(state_);
        fireStatus_(o, old, o.status);
        fireProgressWithWatchers_(o, objectiveProgress_(o));
    }

    void moveToNext_(std::optional<std::size_t> branchFrom, bool onFail) {
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
        index_ = firstEnabledIndexFrom_(index_+1);
        if (!isComplete()) activateObjective_(index_);
    }

    std::size_t firstEnabledIndexFrom_(std::size_t start) const {
        for (std::size_t i=start;i<objectives_.size();++i) if (objectives_[i].enabled) return i;
        return objectives_.size();
    }

    void advance_() {
        if (isComplete()) return;

        while (!isComplete()) {
            auto& cur = objectives_[index_];

            // Fail checks:
            if (cur.failIfLost && state_.lost) { setFail_(cur, "all-colonists-lost"); moveToNext_(index_, /*onFail*/true); break; }
            if (cur.minColonistsAlive && state_.colonistsAlive < *cur.minColonistsAlive) { setFail_(cur, "min-colonists"); moveToNext_(index_, true); break; }
            if (cur.timeoutSeconds && (state_.elapsedSeconds - cur.activatedAt) > *cur.timeoutSeconds) { setFail_(cur, "timeout"); moveToNext_(index_, true); break; }

            // Evaluate:
            const bool selfOk = evalCriteria_(cur.criteria, cur.logic, cur.activatedAt);
            const bool subsOk = evalSubs_(cur);

            if (!(selfOk && subsOk)) { fireProgressWithWatchers_(cur, objectiveProgress_(cur)); break; }

            // Completed a *cycle* of this objective
            cur.repeatCountProgress += 1;
            if (cur.repeatCountProgress < cur.repeatCountTarget) {
                // Repeat: re-arm without leaving step
                if (cur.onComplete) cur.onComplete(state_);
                fireStatus_(cur, Status::Active, Status::Active); // stay active
                // Reset timers & subs for next repetition
                cur.activatedAt = state_.elapsedSeconds;
                for (auto& so : cur.subs) { so.status = Status::Active; so.activatedAt = cur.activatedAt; }
                captureSnapshotsFor_(index_);
                fireProgressWithWatchers_(cur, objectiveProgress_(cur));
                break;
            }

            // Fully complete objective
            const Status old = cur.status;
            cur.status = Status::Completed; cur.completedAt = state_.elapsedSeconds;
            totalScore_ += cur.scoreReward;
            if (cur.onComplete) cur.onComplete(state_);
            fireStatus_(cur, old, cur.status);
            if (cur.checkpoint) lastCheckpoint_ = index_;

            moveToNext_(index_, /*onFail*/false);
            if (isComplete()) break;
        }
    }

    // -------- Evaluation --------
    bool evalCriteria_(const std::vector<Criterion>& list, Logic logic, double activatedAt) const {
        if (list.empty()) return true;
        if (logic == Logic::All) {
            for (const auto& c : list) if (!criterionSatisfied_(c, activatedAt)) return false;
            return true;
        } else {
            for (const auto& c : list) if (criterionSatisfied_(c, activatedAt)) return true;
            return false;
        }
    }

    bool evalSubs_(Objective& o) {
        if (o.subs.empty()) return true;
        int completed = 0;
        for (auto& so : o.subs) {
            if (so.status == Status::Completed) { ++completed; continue; }
            const bool ok = evalCriteria_(so.criteria, so.logic, so.activatedAt);
            if (ok && so.status == Status::Active) {
                so.status = Status::Completed;
                if (so.onComplete) so.onComplete(state_);
            }
            if (so.status == Status::Completed) ++completed;
        }

        if (o.subLogic == Logic::All)   return completed == int(o.subs.size());
        const int need = (o.minSubsToComplete > 0 ? o.minSubsToComplete : 1);
        return completed >= need;
    }

    bool criterionSatisfied_(const Criterion& c, double activatedAt) const {
        bool result = false;
        switch (c.kind) {
            case Criterion::Kind::CounterAtLeast:       result = (readCounter_(c.key) >= c.target); break;
            case Criterion::Kind::CounterAtMost:        result = (readCounter_(c.key) <= c.target); break;
            case Criterion::Kind::CounterEqual:         result = (readCounter_(c.key) == c.target); break;
            case Criterion::Kind::CounterRange:         { auto v=readCounter_(c.key); result = (v>=c.min && v<=c.max); } break;
            case Criterion::Kind::CounterDeltaSinceActivationAtLeast:
                result = (counterDeltaSinceActivation_(c.key, activatedAt) >= c.target); break;
            case Criterion::Kind::CounterInWindowAtLeast:
                result = (counterDeltaInWindow_(c.key, c.windowSecs) >= c.target); break;
            case Criterion::Kind::EventCountInWindowAtLeast:
                result = (eventCountInWindow_("ev:"+c.key, c.windowSecs) >= c.target); break;
            case Criterion::Kind::NoEventInWindow:
                result = noEventInWindow_("ev:"+c.key, c.windowSecs); break;
            case Criterion::Kind::FlagEquals:
                result = ((state_.flags.count(c.key)!=0) == c.expectedFlag); break;
            case Criterion::Kind::TimeElapsed:
                result = ((state_.elapsedSeconds - activatedAt) >= c.seconds); break;
            case Criterion::Kind::Predicate:
                result = (c.predicate ? c.predicate(state_) : false); break;
        }
        return c.negate ? !result : result;
    }

    float criterionProgress_(const Criterion& c, double activatedAt) const {
        auto clamp01 = [](double x){ return float(x<0?0:(x>1?1:x)); };
        switch (c.kind) {
            case Criterion::Kind::CounterAtLeast: {
                double v = double(readCounter_(c.key));
                if (c.target <= 0) return 1.f; return clamp01(v / double(c.target));
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
                if (c.max <= c.min) return v>=c.min ? 1.f : 0.f;
                return clamp01((v - c.min) / double(c.max - c.min));
            }
            case Criterion::Kind::CounterDeltaSinceActivationAtLeast: {
                const double v = double(counterDeltaSinceActivation_(c.key, activatedAt));
                if (c.target <= 0) return 1.f; return clamp01(v / double(c.target));
            }
            case Criterion::Kind::CounterInWindowAtLeast: {
                const double v = double(counterDeltaInWindow_(c.key, c.windowSecs));
                if (c.target <= 0) return 1.f; return clamp01(v / double(c.target));
            }
            case Criterion::Kind::EventCountInWindowAtLeast: {
                const double v = double(eventCountInWindow_("ev:"+c.key, c.windowSecs));
                if (c.target <= 0) return 1.f; return clamp01(v / double(c.target));
            }
            case Criterion::Kind::NoEventInWindow: {
                const bool ok = noEventInWindow_("ev:"+c.key, c.windowSecs);
                const double last = timeSinceLastEvent_("ev:"+c.key);
                const double p = (c.windowSecs<=0.0) ? (ok?1.0:0.0) : std::clamp(1.0 - (last / c.windowSecs), 0.0, 1.0);
                return clamp01(ok ? 1.0 : p);
            }
            case Criterion::Kind::FlagEquals: {
                const bool ok = (state_.flags.count(c.key)!=0) == c.expectedFlag; return ok ? 1.f : 0.f;
            }
            case Criterion::Kind::TimeElapsed: {
                if (c.seconds <= 0.0) return 1.f;
                const double since = state_.elapsedSeconds - activatedAt; return clamp01(since / c.seconds);
            }
            case Criterion::Kind::Predicate: {
                const bool ok = (c.predicate ? c.predicate(state_) : false); return ok ? 1.f : 0.f;
            }
        }
        return 0.f;
    }

    float objectiveProgress_(const Objective& o) const {
        // Weighted mean of own criteria + sub-criteria.
        double sumW=0.0, sum=0.0;

        if (!o.criteria.empty()) {
            double w=0.0, s=0.0;
            for (const auto& c : o.criteria) {
                const double cw = (c.weight>0.0 ? c.weight : 1.0);
                const double cp = criterionProgress_(c, o.activatedAt);
                w += cw; s += cw*cp;
            }
            sumW += w; sum += s;
        }

        if (!o.subs.empty()) {
            double w=0.0, s=0.0;
            for (const auto& so : o.subs) {
                double lw=0.0, ls=0.0;
                for (const auto& c : so.criteria) {
                    const double cw = (c.weight>0.0 ? c.weight : 1.0);
                    const double cp = criterionProgress_(c, so.activatedAt);
                    lw += cw; ls += cw*cp;
                }
                const double p = (lw>0.0? ls/lw : 1.0);
                w += 1.0; s += p; // equal weight for subs
            }
            sumW += w; sum += s;
        }

        return float(sumW>0.0 ? (sum/sumW) : 1.0);
    }

    // -------- Telemetry-backed helpers --------
    std::int64_t counterDeltaInWindow_(const std::string& ctrName, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
        if (windowSecs <= 0.0) return readCounter_(ctrName);
        const double t0 = state_.elapsedSeconds - windowSecs;
        const std::string full = "ctr:"+ctrName;
        std::int64_t sum=0;
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            if (it->t < t0) break;
            if (it->name == full) sum += it->delta;
        }
        return sum;
#else
        (void)windowSecs; return readCounter_(ctrName);
#endif
    }

    std::int64_t counterDeltaSinceActivation_(const std::string& ctrName, double activatedAt) const {
#if SLICE_OT_ENABLE_TELEMETRY
        // Sum deltas since activation
        const double t0 = activatedAt;
        const std::string full = "ctr:"+ctrName;
        std::int64_t sum=0;
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
            const std::int64_t base = (cit==oit->second.end()) ? 0 : cit->second;
            return readCounter_(ctrName) - base;
        }
        return readCounter_(ctrName); // best-effort
#endif
    }

    std::int64_t eventCountInWindow_(const std::string& fullEvent, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
        if (windowSecs <= 0.0) return 0;
        const double t0 = state_.elapsedSeconds - windowSecs;
        std::int64_t cnt=0;
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            if (it->t < t0) break;
            if (it->name == fullEvent) cnt += it->delta;
        }
        return cnt;
#else
        (void)fullEvent; (void)windowSecs; return 0;
#endif
    }

    bool noEventInWindow_(const std::string& fullEvent, double windowSecs) const {
#if SLICE_OT_ENABLE_TELEMETRY
        if (windowSecs <= 0.0) return true;
        const double t0 = state_.elapsedSeconds - windowSecs;
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            if (it->t < t0) break;
            if (it->name == fullEvent) return false;
        }
        return true;
#else
        (void)fullEvent; (void)windowSecs; return false;
#endif
    }

    double timeSinceLastEvent_(const std::string& fullEvent) const {
#if SLICE_OT_ENABLE_TELEMETRY
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            if (it->name == fullEvent) return std::max(0.0, state_.elapsedSeconds - it->t);
        }
        return std::numeric_limits<double>::infinity();
#else
        (void)fullEvent; return std::numeric_limits<double>::infinity();
#endif
    }

    // -------- Counters & logging --------
    void notifyCounterImpl_(std::string_view name, std::int64_t delta) {
        auto& v = state_.counters[std::string(name)];
        v += delta;
        logEvent_("ctr:"+std::string(name), delta, v);
        advance_();
    }

    void notifyEventUnlocked_(std::string_view eventName, std::int64_t count) {
        logEvent_("ev:"+std::string(eventName), count, count);
    }

    std::int64_t readCounter_(const std::string& name) const {
        auto it = state_.counters.find(name);
        return (it==state_.counters.end()) ? 0 : it->second;
    }

    void logEvent_(const std::string& name, std::int64_t delta, std::int64_t value) {
#if SLICE_OT_ENABLE_TELEMETRY
        if constexpr (SLICE_OT_LOG_CAPACITY > 0) {
            if (log_.size() >= SLICE_OT_LOG_CAPACITY) log_.pop_front();
            log_.push_back(EventRecord{ state_.elapsedSeconds, name, delta, value });
        }
#else
        (void)name; (void)delta; (void)value;
#endif
    }

    // -------- Snapshots (used if telemetry disabled) --------
    void captureSnapshotsFor_(std::size_t idx) {
#if !SLICE_OT_ENABLE_TELEMETRY
        if (idx >= objectives_.size()) return;
        std::unordered_map<std::string, std::int64_t> snap;
        const auto& o = objectives_[idx];
        auto capture = [&](const Criterion& c){
            if (c.kind==Criterion::Kind::CounterDeltaSinceActivationAtLeast ||
                c.kind==Criterion::Kind::CounterInWindowAtLeast ||
                c.kind==Criterion::Kind::CounterAtLeast ||
                c.kind==Criterion::Kind::CounterAtMost ||
                c.kind==Criterion::Kind::CounterEqual ||
                c.kind==Criterion::Kind::CounterRange) {
                snap[c.key] = readCounter_(c.key);
            }
        };
        for (const auto& c : o.criteria) capture(c);
        for (const auto& so : o.subs) for (const auto& c : so.criteria) capture(c);
        counterSnapshots_[idx] = std::move(snap);
#else
        (void)idx;
#endif
    }

    // -------- Formatting helpers --------
    static std::string mmss_(double seconds) {
        if (seconds < 0.0) seconds = 0.0;
        int s = int(seconds + 0.5);
        int m = s / 60; s %= 60;
        std::ostringstream oss; oss << std::setfill('0') << std::setw(2) << m << ":" << std::setw(2) << s;
        return oss.str();
    }
    static std::string percent_(double p01) {
        if (p01 < 0.0) p01 = 0.0; else if (p01 > 1.0) p01 = 1.0;
        const int pct = int(p01 * 100.0 + 0.5);
        return std::to_string(pct) + "%";
    }
    static std::string progressBar_(double p01, int width) {
        if (width <= 0) return "";
        if (p01 < 0) p01 = 0; else if (p01 > 1) p01 = 1;
        int full = int(p01 * width + 0.5);
        std::string s; s.reserve(size_t(width)+2);
        s.push_back('[');
        for (int i=0;i<width;++i) s.push_back(i<full ? '#' : '-');
        s.push_back(']');
        return s;
    }
    static std::string statusTag_(Status st) {
        switch (st) {
            case Status::Locked:    return " [LOCKED]";
            case Status::Active:    return " [ACTIVE]";
            case Status::Completed: return " [DONE]";
            case Status::Failed:    return " [FAILED]";
            case Status::Skipped:   return " [SKIPPED]";
        }
        return {};
    }
    std::string hudLabel_(const Criterion& c) const {
        if (!c.label.empty()) return loc_(c.label);
        switch (c.kind) {
            case Criterion::Kind::CounterAtLeast: return c.key + " >= " + std::to_string(c.target);
            case Criterion::Kind::CounterAtMost:  return c.key + " <= " + std::to_string(c.target);
            case Criterion::Kind::CounterEqual:   return c.key + " == " + std::to_string(c.target);
            case Criterion::Kind::CounterRange:   return c.key + " in [" + std::to_string(c.min) + "," + std::to_string(c.max) + "]";
            case Criterion::Kind::CounterDeltaSinceActivationAtLeast: return c.key + " +" + std::to_string(c.target) + " since start";
            case Criterion::Kind::CounterInWindowAtLeast: return c.key + " +" + std::to_string(c.target) + " in last " + std::to_string(int(c.windowSecs)) + "s";
            case Criterion::Kind::EventCountInWindowAtLeast: return c.key + " x" + std::to_string(c.target) + " in " + std::to_string(int(c.windowSecs)) + "s";
            case Criterion::Kind::NoEventInWindow: return "No " + c.key + " in " + std::to_string(int(c.windowSecs)) + "s";
            case Criterion::Kind::FlagEquals:   return c.key + (c.expectedFlag ? " ON" : " OFF");
            case Criterion::Kind::TimeElapsed:  return "Time " + std::to_string(int(c.seconds)) + "s";
            case Criterion::Kind::Predicate:    return "Predicate";
        }
        return {};
    }
    std::string extraDetail_(const Criterion& c, double activatedAt) const {
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
            oss << "  (" << counterDeltaInWindow_(c.key, c.windowSecs) << " / " << c.target
                << " in " << int(c.windowSecs) << "s)";
        } else if (c.kind == Criterion::Kind::EventCountInWindowAtLeast) {
            oss << "  (" << eventCountInWindow_("ev:"+c.key, c.windowSecs) << " / " << c.target
                << " in " << int(c.windowSecs) << "s)";
        } else if (c.kind == Criterion::Kind::TimeElapsed) {
            const double since = state_.elapsedSeconds - activatedAt;
            oss << "  (" << mmss_(since) << " / " << mmss_(c.seconds) << ")";
        }
        return oss.str();
    }

    // -------- Status / progress firing --------
    void fireProgressWithWatchers_(const Objective& o, float prog) {
        // global progress callback
        if (onProgress_) onProgress_(o, prog, state_);
        // threshold watchers (fire once when crossing upwards)
        const std::size_t idx = findIndexUnsafe_(o.id);
        if (idx < lastProgress_.size()) {
            const float prev = lastProgress_[idx];
            lastProgress_[idx] = prog;
            for (auto& w : watchers_) {
                if (w.objectiveIndex == idx && !w.fired && prev < w.threshold && prog >= w.threshold) {
                    w.fired = true; if (w.cb) w.cb(o, w.threshold, state_);
                }
            }
        }
    }

    void fireStatus_(const Objective& o, Status old, Status now) {
        if (onStatus_) onStatus_(o, old, now, state_);
    }

    void setFail_(Objective& o, std::string reason) {
        const Status old = o.status;
        o.status = Status::Failed; o.lastFailReason = std::move(reason);
        totalScore_ -= o.scorePenaltyOnFail;
        if (o.onFail) o.onFail(state_);
        fireStatus_(o, old, o.status);
    }

    std::size_t findIndexUnsafe_(const std::string& id) const {
        for (std::size_t i=0;i<objectives_.size();++i) if (objectives_[i].id==id) return i;
        return objectives_.size();
    }

    // -------- Localization --------
    std::string loc_(const std::string& s) const {
        if (!localize_) return s;
        if (!s.empty() && s[0] == locPrefix_) {
            return localize_(std::string_view(s).substr(1));
        }
        return s;
    }

private:
    struct ThresholdWatcher {
        std::size_t objectiveIndex;
        double threshold;
        bool fired;
        ThresholdCallback cb;
    };

    std::vector<Objective> objectives_;
    std::size_t  index_ = static_cast<std::size_t>(-1);
    SliceState   state_{};
    int          totalScore_ = 0;
    std::optional<std::size_t> lastCheckpoint_;

    // Observers
    ProgressCallback onProgress_;
    StatusCallback   onStatus_;
    std::vector<ThresholdWatcher> watchers_;
    std::vector<float> lastProgress_; // per objective

#if SLICE_OT_ENABLE_TELEMETRY
    std::deque<EventRecord> log_;
#else
    // Snapshots of counters when an objective is (re)activated. [objective index] -> {counterName -> value}
    std::unordered_map<std::size_t, std::unordered_map<std::string, std::int64_t>> counterSnapshots_;
#endif

#if SLICE_OT_THREAD_SAFE
    mutable std::mutex mtx_;
#endif

    LocalizeFn localize_;
    char       locPrefix_ = '$';
};

// ============================ End slice namespace =============================
} // namespace slice

#if defined(_MSC_VER)
  #pragma warning(pop)
#endif
