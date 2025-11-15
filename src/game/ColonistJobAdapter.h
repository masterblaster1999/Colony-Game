#pragma once

#include <unordered_map>
#include "jobs/JobSystem.h"

// These are placeholders. Replace with your real types.
struct Colonist
{
    colony::jobs::AgentId id;
    int tileX;
    int tileY;

    bool isIdle = true;
    // ... whatever else you have ...
};

class ColonistJobAdapter : public colony::jobs::IAgentAdapter
{
public:
    // Map from AgentId -> Colonist*
    std::unordered_map<colony::jobs::AgentId, Colonist*> colonists;

    bool isAgentIdle(colony::jobs::AgentId agent) const override
    {
        auto it = colonists.find(agent);
        if (it == colonists.end() || it->second == nullptr)
            return false;

        return it->second->isIdle;
    }

    colony::jobs::Int2 getAgentTile(colony::jobs::AgentId agent) const override
    {
        colony::jobs::Int2 result{};
        auto it = colonists.find(agent);
        if (it != colonists.end() && it->second != nullptr)
        {
            result.x = it->second->tileX;
            result.y = it->second->tileY;
        }
        return result;
    }

    void assignJobToAgent(colony::jobs::AgentId agent, const colony::jobs::Job& job) override
    {
        auto it = colonists.find(agent);
        if (it == colonists.end() || it->second == nullptr)
            return;

        Colonist* c = it->second;
        c->isIdle = false;

        // Here you would set the colonist's internal state to start that job:
        // - pathfind toward job.targetTile
        // - set some "currentJobId" if you track that
        // - etc.
        (void)job; // To silence unused parameter warning until you hook it up.
    }
};
