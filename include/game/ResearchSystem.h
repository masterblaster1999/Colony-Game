// include/game/ResearchSystem.h
#pragma once
#include "ResearchDef.h"
#include <unordered_set>
#include <unordered_map>
#include <optional>

struct Colony;  // forward declare your existing Colony

struct ResearchProgress
{
    float accumulated = 0.0f; // research points accumulated
    int   cost        = 0;    // cached from ResearchDef
};

class ResearchSystem
{
public:
    // Must be called once with the static definitions table
    explicit ResearchSystem(const std::vector<ResearchDef>& defs);

    // Query
    bool IsUnlocked(ResearchId id) const;
    bool IsInProgress(ResearchId id) const;
    std::optional<ResearchId> CurrentResearch() const;

    // Control
    bool CanStart(ResearchId id) const;
    bool StartResearch(ResearchId id);
    void CancelCurrent();

    // Tick – called once per frame with dt in seconds.
    // Returns a list of newly-completed research IDs.
    std::vector<ResearchId> Tick(double dt, const Colony& colony);

    // Access for save/load
    const std::unordered_set<ResearchId>& Completed() const { return completed_; }
    void SetCompleted(const std::unordered_set<ResearchId>& ids);

private:
    const std::vector<ResearchDef>* defs_ = nullptr;
    std::unordered_map<ResearchId, ResearchProgress> progress_;
    std::unordered_set<ResearchId> completed_;
    std::optional<ResearchId> current_;
};
