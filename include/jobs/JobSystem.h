#pragma once

#include <vector>
#include "jobs/JobQueue.h"

namespace colony::jobs
{
    // This interface is how the job system talks to your actual colonist/world code.
    // You implement this in your game layer.
    struct IAgentAdapter
    {
        virtual ~IAgentAdapter() = default;

        // Should return true if the agent is currently not doing anything important
        // and is ready to receive a new job.
        virtual bool isAgentIdle(AgentId agent) const = 0;

        // World grid position of the agent (used for proximity scoring).
        virtual Int2 getAgentTile(AgentId agent) const = 0;

        // Called when the job system has assigned a job to an agent.
        // Your implementation should set whatever state is needed on the colonist.
        virtual void assignJobToAgent(AgentId agent, const Job& job) = 0;
    };

    // Central orchestrator:
    // - Holds the JobQueue
    // - Knows about registered agents
    // - On update(), assigns jobs to idle agents
    class JobSystem
    {
    public:
        explicit JobSystem(IAgentAdapter& agentAdapter);

        // Create a job and add it to the queue.
        JobId createJob(JobType type, const Int2& targetTile, int priority = 0);

        // Mark that an agent finished a job successfully.
        void notifyJobCompleted(JobId jobId, AgentId agent);

        // Mark that an agent failed a job (blocked, unreachable, etc.).
        // Job is reopened for others.
        void notifyJobFailed(JobId jobId, AgentId agent);

        // Register / unregister agents with the system.
        void registerAgent(AgentId agent);
        void unregisterAgent(AgentId agent);

        // Main update: looks for idle agents and assigns jobs.
        void update(float dt);

        // Direct access to the underlying queue if needed.
        JobQueue&       queue()       { return queue_; }
        const JobQueue& queue() const { return queue_; }

    private:
        IAgentAdapter&     agentAdapter_;
        JobQueue           queue_;
        std::vector<AgentId> agents_;
    };

} // namespace colony::jobs
