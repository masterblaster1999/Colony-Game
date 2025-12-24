// src/slice/ObjectiveTypes.h
#pragma once
/*
    ObjectiveTypes.h — Objective & Achievement System types
    ------------------------------------------------------
    This header contains the *data model* used by the objective tracker:
      - Status / Logic enums
      - SliceState
      - Criterion (+ factories)
      - SubObjective (+ Builder)
      - Objective (+ Builder)

    The runtime engine lives in:
      - ObjectiveTracker.h / ObjectiveTracker.cpp

    Notes:
      - C++17
      - No hard deps; optional JSON integration is in ObjectiveTracker.h
*/

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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
#endif
#ifndef SLICE_OT_VERSION_MINOR
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

namespace slice {

// ================================ Utilities ==================================

#if SLICE_OT_ENABLE_HASH
// constexpr FNV-1a 64-bit (ASCII/UTF-8). Non-cryptographic.
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
        // Store fields first; construct SubObjective in build()
        std::string id_;
        std::string title_;
        Logic       logic_ = Logic::All;
        std::vector<Criterion> criteria_;
        std::function<void(SliceState&)> onActivate_;
        std::function<void(SliceState&)> onComplete_;
        std::function<void(SliceState&)> onFail_;

        explicit Builder(std::string id) : id_(std::move(id)) {}
        Builder& title(std::string t){ title_ = std::move(t); return *this; }
        Builder& allOf(std::vector<Criterion> v){ logic_ = Logic::All; criteria_ = std::move(v); return *this; }
        Builder& anyOf(std::vector<Criterion> v){ logic_ = Logic::Any; criteria_ = std::move(v); return *this; }
        Builder& onActivateFn(std::function<void(SliceState&)> f){ onActivate_ = std::move(f); return *this; }
        Builder& onCompleteFn(std::function<void(SliceState&)> f){ onComplete_ = std::move(f); return *this; }
        Builder& onFailFn(std::function<void(SliceState&)> f){ onFail_ = std::move(f); return *this; }

        SubObjective build(){
            SubObjective so;
            so.id = std::move(id_);
            so.title = std::move(title_);
            so.logic = logic_;
            so.criteria = std::move(criteria_);
            so.onActivate = std::move(onActivate_);
            so.onComplete = std::move(onComplete_);
            so.onFail = std::move(onFail_);
            return so;
        }
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
        // Store fields; materialize Objective in build()
        std::string id_;
        std::string title_;
        std::string description_;
        Logic logic_ = Logic::All;
        std::vector<Criterion> criteria_;
        // Sub objectives
        Logic subLogic_ = Logic::All;
        int   minSubsToComplete_ = -1;
        std::vector<SubObjective> subs_;
        // Fail rules
        std::optional<int>    minColonistsAlive_;
        std::optional<double> timeoutSeconds_;
        bool                  failIfLost_ = true;
        // Scoring & progression
        int    scoreReward_ = 0;
        int    scorePenaltyOnFail_ = 0;
        double weight_ = 1.0;
        bool   checkpoint_ = false;
        int    repeatCountTarget_ = 1;
        // Branching
        std::string nextOnCompleteId_;
        std::string nextOnFailId_;
        // Enable/disable
        bool enabled_ = true;
        // Callbacks
        std::function<void(SliceState&)> onActivate_;
        std::function<void(SliceState&)> onComplete_;
        std::function<void(SliceState&)> onFail_;

        explicit Builder(std::string id) : id_(std::move(id)) {}
        Builder& title(std::string t){ title_ = std::move(t); return *this; }
        Builder& desc(std::string d){ description_ = std::move(d); return *this; }
        Builder& allOf(std::vector<Criterion> v){ logic_ = Logic::All; criteria_ = std::move(v); return *this; }
        Builder& anyOf(std::vector<Criterion> v){ logic_ = Logic::Any; criteria_ = std::move(v); return *this; }
        Builder& subAll(std::vector<SubObjective> v){ subLogic_ = Logic::All; subs_ = std::move(v); return *this; }
        Builder& subAny(std::vector<SubObjective> v, int minCount = 1){ subLogic_ = Logic::Any; subs_ = std::move(v); minSubsToComplete_ = std::max(1, minCount); return *this; }
        Builder& minColonists(int n){ minColonistsAlive_ = n; return *this; }
        Builder& timeout(double secs){ timeoutSeconds_ = secs; return *this; }
        Builder& reward(int s){ scoreReward_ = s; return *this; }
        Builder& penaltyOnFail(int s){ scorePenaltyOnFail_ = s; return *this; }
        Builder& weight(double w){ weight_ = w; return *this; }
        Builder& markCheckpoint(bool v=true){ checkpoint_ = v; return *this; }
        Builder& repeatable(int times){ repeatCountTarget_ = std::max(1, times); return *this; }
        Builder& nextOnComplete(std::string id){ nextOnCompleteId_ = std::move(id); return *this; }
        Builder& nextOnFail(std::string id){ nextOnFailId_ = std::move(id); return *this; }
        Builder& onActivateFn(std::function<void(SliceState&)> f){ onActivate_ = std::move(f); return *this; }
        Builder& onCompleteFn(std::function<void(SliceState&)> f){ onComplete_ = std::move(f); return *this; }
        Builder& onFailFn(std::function<void(SliceState&)> f){ onFail_ = std::move(f); return *this; }

        Objective build(){
            Objective o;
            o.id = std::move(id_);
            o.title = std::move(title_);
            o.description = std::move(description_);
            // own criteria
            o.logic = logic_;
            o.criteria = std::move(criteria_);
            // sub objectives
            o.subLogic = subLogic_;
            o.minSubsToComplete = minSubsToComplete_;
            o.subs = std::move(subs_);
            // fail rules
            o.minColonistsAlive = minColonistsAlive_;
            o.timeoutSeconds    = timeoutSeconds_;
            o.failIfLost        = failIfLost_;
            // scoring & progression
            o.scoreReward       = scoreReward_;
            o.scorePenaltyOnFail= scorePenaltyOnFail_;
            o.weight            = weight_;
            o.checkpoint        = checkpoint_;
            o.repeatCountTarget = repeatCountTarget_;
            // branching
            o.nextOnCompleteId  = std::move(nextOnCompleteId_);
            o.nextOnFailId      = std::move(nextOnFailId_);
            // enable/disable
            o.enabled           = enabled_;
            // callbacks
            o.onActivate        = std::move(onActivate_);
            o.onComplete        = std::move(onComplete_);
            o.onFail            = std::move(onFail_);
            return o;
        }
    };
};

} // namespace slice

#if defined(_MSC_VER)
  #pragma warning(pop)
#endif
