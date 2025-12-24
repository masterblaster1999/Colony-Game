// src/slice/ObjectiveTracker.h
#pragma once
/*
    ObjectiveTracker.h — Objective & Achievement System runtime (C++17)
    ------------------------------------------------------------------
    Split layout (for faster incremental builds and cleaner layering):
      - ObjectiveTypes.h      : enums + data model + builders
      - ObjectiveTracker.h    : tracker declaration (this file)
      - ObjectiveTracker.cpp  : tracker implementation

    Notes:
      - This module is self-contained and does not depend on the rest of the engine.
      - Compile-time configuration macros (SLICE_OT_*) are declared in ObjectiveTypes.h
        and must be consistent across all translation units that include these headers.
*/

#include "ObjectiveTypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

class ObjectiveTracker {
public:
    // Telemetry record (counters/flags/events). Window criteria read from here.
    struct EventRecord {
        double       t = 0.0;          // slice time (seconds)
        std::string  name;             // "ctr:<k>", "flg:<k>", "ev:<k>"
        std::int64_t delta = 0;        // +N / -N for counters; ±1 for flags; +count for events
        std::int64_t value = 0;        // resulting absolute value (flags: 0/1)
    };

    // Callbacks
    using ProgressCallback  = std::function<void(const Objective&, float /*0..1*/, const SliceState&)>;
    using StatusCallback    = std::function<void(const Objective&, Status /*old*/, Status /*now*/, const SliceState&)>;
    using ThresholdCallback = std::function<void(const Objective&, double /*threshold*/, const SliceState&)>;
    using LocalizeFn        = std::function<std::string(std::string_view token)>;

    ObjectiveTracker() = default;

    // ---------------- Lifecycle ----------------
    void reset();
    void start();

    void pause(bool v);
    void resume();
    void setTimeScale(double s);
    void setLocalizer(LocalizeFn fn);
    void setHudTokenPrefix(char ch);

    // Call once per frame with *unscaled* dt (seconds).
    void update(double dtSeconds);

    // ---------------- Build & query ----------------
    Objective& add(Objective o);
    Objective& add(Objective::Builder b);

    bool isComplete() const { return objectives_.empty() || index_ >= objectives_.size(); }
    bool isFailed() const { return !isComplete() && objectives_[index_].status == Status::Failed; }

    std::size_t objectiveCount() const { return objectives_.size(); }
    const Objective* current() const;
    std::size_t currentIndex() const { return index_; }
    std::optional<std::size_t> findById(std::string_view id) const;

    // Enable/disable + branching helpers
    bool setEnabledById(std::string_view id, bool enabled);
    bool skipCurrent();
    bool failCurrent(std::string reason = "debug-fail");
    bool setCurrentById(std::string_view id);

    // ---------------- Event API ----------------
    // Built-ins:
    void notifyStructureBuilt(int count = 1);
    void notifyItemCrafted(int count = 1);
    void notifyColonistSpawned(int count = 1);
    void notifyColonistDied(int count = 1);

    // Generic counters/flags/events:
    void notifyCounter(std::string_view name, std::int64_t delta = 1);
    void setCounter(std::string_view name, std::int64_t value);
    std::int64_t getCounter(std::string_view name) const;
    void setFlag(std::string_view name, bool value = true);
    bool getFlag(std::string_view name) const;
    void notifyEvent(std::string_view eventName, std::int64_t count = 1);

    // ---------------- HUD / Progress ----------------
    struct HudOptions {
        int  barWidth = 18;
        bool showCounters = true;
        bool showFlags = false;
        bool showTimer = true;
        bool showSubObjectives = true;
        bool showCompletedCheck = true;
    };

    // No-arg convenience overload (kept to preserve the old default-arg API).
    std::vector<std::string> hudLines() const;
    std::vector<std::string> hudLines(const HudOptions& opt) const;
    double overallProgress() const;

    // ---------------- Observability ----------------
    void setProgressCallback(ProgressCallback cb);
    void setStatusCallback(StatusCallback cb);

    // Fire callback the first time an objective's progress crosses a threshold (0..1].
    bool addProgressThresholdWatcherById(std::string_view id, double threshold, ThresholdCallback cb);

    // ---------------- Checkpoints ----------------
    std::optional<std::size_t> lastCheckpointIndex() const { return lastCheckpoint_; }
    bool restoreToLastCheckpoint();

    // ---------------- Save / Load ----------------
    std::string serialize() const;
    bool deserialize(std::string_view data);

#if SLICE_OT_USE_NLOHMANN_JSON
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["ver"] = { {"major", SLICE_OT_VERSION_MAJOR}, {"minor", SLICE_OT_VERSION_MINOR} };
        j["time"] = { {"elapsed", state_.elapsedSeconds}, {"scale", state_.timeScale}, {"paused", state_.paused} };
        j["builtins"] = { {"structuresBuilt", state_.structuresBuilt}, {"itemsCrafted", state_.itemsCrafted}, {"colonistsAlive", state_.colonistsAlive}, {"lost", state_.lost} };
        j["score"] = totalScore_;
        j["index"] = index_;
        j["checkpoint"] = lastCheckpoint_.has_value() ? nlohmann::json(*lastCheckpoint_) : nlohmann::json();
        j["counters"] = state_.counters;
        j["flags"] = std::vector<std::string>(state_.flags.begin(), state_.flags.end());

        std::vector<nlohmann::json> objs;
        objs.reserve(objectives_.size());
        for (const auto& o : objectives_) {
            nlohmann::json jo{
                {"id", o.id}, {"title", o.title}, {"desc", o.description},
                {"status", int(o.status)}, {"activatedAt", o.activatedAt}, {"completedAt", o.completedAt},
                {"repeatProg", o.repeatCountProgress}, {"repeatTarget", o.repeatCountTarget},
                {"scoreReward", o.scoreReward}, {"penaltyOnFail", o.scorePenaltyOnFail}
            };
            if (!o.subs.empty()) {
                std::vector<nlohmann::json> subs;
                subs.reserve(o.subs.size());
                for (const auto& so : o.subs) {
                    subs.push_back({ {"id", so.id}, {"title", so.title}, {"status", int(so.status)}, {"activatedAt", so.activatedAt} });
                }
                jo["subs"] = std::move(subs);
            }
            objs.push_back(std::move(jo));
        }
        j["objectives"] = std::move(objs);
        return j;
    }

    bool from_json(const nlohmann::json& j) {
        SLICE_OT_LOCK_GUARD;

        state_.clear();
        totalScore_ = j.value("score", 0);
        index_ = j.value("index", std::size_t(0));

        if (j.contains("time")) {
            const auto& t = j["time"];
            state_.elapsedSeconds = t.value("elapsed", 0.0);
            state_.timeScale = t.value("scale", 1.0);
            state_.paused = t.value("paused", false);
        }
        if (j.contains("builtins")) {
            const auto& b = j["builtins"];
            state_.structuresBuilt = b.value("structuresBuilt", 0);
            state_.itemsCrafted = b.value("itemsCrafted", 0);
            state_.colonistsAlive = b.value("colonistsAlive", 3);
            state_.lost = b.value("lost", false);
        }

        state_.counters = j.value("counters", decltype(state_.counters){});
        state_.flags.clear();
        if (j.contains("flags")) {
            for (const auto& f : j["flags"]) {
                state_.flags.insert(f.get<std::string>());
            }
        }
        if (j.contains("checkpoint") && !j["checkpoint"].is_null()) {
            lastCheckpoint_ = j["checkpoint"].get<std::size_t>();
        } else {
            lastCheckpoint_.reset();
        }

        if (j.contains("objectives")) {
            auto arr = j["objectives"];
            for (std::size_t i = 0; i < arr.size() && i < objectives_.size(); ++i) {
                auto& o = objectives_[i];
                const auto& jo = arr[i];
                if (jo.value("id", o.id) == o.id) {
                    o.status = Status(jo.value("status", int(o.status)));
                    o.activatedAt = jo.value("activatedAt", 0.0);
                    o.completedAt = jo.value("completedAt", 0.0);
                    o.repeatCountProgress = jo.value("repeatProg", 0);
                    o.repeatCountTarget = jo.value("repeatTarget", 1);
                    if (jo.contains("subs")) {
                        auto sarr = jo["subs"];
                        for (std::size_t k = 0; k < sarr.size() && k < o.subs.size(); ++k) {
                            auto& so = o.subs[k];
                            const auto& js = sarr[k];
                            if (js.value("id", so.id) == so.id) {
                                so.status = Status(js.value("status", int(so.status)));
                                so.activatedAt = js.value("activatedAt", 0.0);
                            }
                        }
                    }
                }
            }
        }
        if (!objectives_.empty() && index_ < objectives_.size() && objectives_[index_].status != Status::Active) {
            activateObjective_(index_);
        }
        return true;
    }
#endif // SLICE_OT_USE_NLOHMANN_JSON

    // ---------------- Pre-canned default slice ----------------
    static ObjectiveTracker MakeDefault(double surviveSeconds, int structuresToBuild, int itemsToCraft, int startingColonists);

    // ---------------- Accessors ----------------
    int score() const { return totalScore_; }
    const SliceState& state() const { return state_; }
    SliceState& state() { return state_; }

#if SLICE_OT_ENABLE_TELEMETRY
    std::vector<EventRecord> recentEvents(std::size_t maxCount = 64) const;
#endif

private:
    // -------- Small string helper --------
    static std::string makeKey_(std::string_view ns, std::string_view name);

    // -------- Activation & sequencing --------
    void activateObjective_(std::size_t idx);
    void moveToNext_(std::optional<std::size_t> branchFrom, bool onFail);
    std::size_t firstEnabledIndexFrom_(std::size_t start) const;
    void advance_();

    // -------- Evaluation --------
    bool evalCriteria_(const std::vector<Criterion>& list, Logic logic, double activatedAt) const;
    bool evalSubs_(Objective& o);
    bool criterionSatisfied_(const Criterion& c, double activatedAt) const;
    float criterionProgress_(const Criterion& c, double activatedAt) const;
    float objectiveProgress_(const Objective& o) const;

    // -------- Telemetry-backed helpers --------
    std::int64_t counterDeltaInWindow_(const std::string& ctrName, double windowSecs) const;
    std::int64_t counterDeltaSinceActivation_(const std::string& ctrName, double activatedAt) const;
    std::int64_t eventCountInWindow_(const std::string& fullEvent, double windowSecs) const;
    bool noEventInWindow_(const std::string& fullEvent, double windowSecs) const;
    double timeSinceLastEvent_(const std::string& fullEvent) const;

    // -------- Counters & logging --------
    void notifyCounterImpl_(std::string_view name, std::int64_t delta);
    void notifyEventUnlocked_(std::string_view eventName, std::int64_t count);
    std::int64_t readCounter_(const std::string& name) const;
    void logEvent_(std::string name, std::int64_t delta, std::int64_t value);

    // -------- Snapshots (used if telemetry disabled) --------
    void captureSnapshotsFor_(std::size_t idx);

    // -------- Formatting helpers --------
    static std::string mmss_(double seconds);
    static std::string percent_(double p01);
    static std::string progressBar_(double p01, int width);
    static std::string statusTag_(Status st);
    std::string hudLabel_(const Criterion& c) const;
    std::string extraDetail_(const Criterion& c, double activatedAt) const;

    // -------- Status / progress firing --------
    void fireProgressWithWatchers_(const Objective& o, float prog);
    void fireStatus_(const Objective& o, Status old, Status now);
    void setFail_(Objective& o, std::string reason);
    std::size_t findIndexUnsafe_(const std::string& id) const;

    // -------- Localization --------
    std::string loc_(const std::string& s) const;

private:
    struct ThresholdWatcher {
        std::size_t objectiveIndex = 0;
        double threshold = 0.0;
        bool fired = false;
        ThresholdCallback cb;
    };

    std::vector<Objective> objectives_;
    std::size_t index_ = static_cast<std::size_t>(-1);
    SliceState state_{};
    int totalScore_ = 0;
    std::optional<std::size_t> lastCheckpoint_;

    // Observers
    ProgressCallback onProgress_;
    StatusCallback onStatus_;
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
    char locPrefix_ = '$';
};

} // namespace slice
