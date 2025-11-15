#include "jobs/JobSystem.h"

#include <algorithm> // std::find
#include <cmath>     // std::abs

namespace colony::jobs
{
    JobSystem::JobSystem(IAgentAdapter& agentAdapter)
        : agentAdapter_(agentAdapter)
    {
    }

    JobId JobSystem::createJob(JobType type, const Int2& targetTile, int priority)
    {
        return queue_.addJob(type, targetTile, priority);
    }

    void JobSystem::notifyJobCompleted(JobId jobId, AgentId agent)
    {
        Job* job = queue_.getJob(jobId);
        if (!job)
            return;

        if (job->assignedAgent != agent)
            return; // Optional safety check

        job->state = JobState::Completed;
        job->assignedAgent = 0;
    }

    void JobSystem::notifyJobFailed(JobId jobId, AgentId agent)
    {
        Job* job = queue_.getJob(jobId);
        if (!job)
            return;

        if (job->assignedAgent != agent)
            return; // Optional safety check

        // Re-open the job for someone else to try.
        job->state = JobState::Open;
        job->assignedAgent = 0;
    }

    void JobSystem::registerAgent(AgentId agent)
    {
        auto it = std::find(agents_.begin(), agents_.end(), agent);
        if (it == agents_.end())
        {
            agents_.push_back(agent);
        }
    }

    void JobSystem::unregisterAgent(AgentId agent)
    {
        auto it = std::find(agents_.begin(), agents_.end(), agent);
        if (it != agents_.end())
        {
            agents_.erase(it);
        }
    }

    void JobSystem::update(float dt)
    {
        // dt is currently unused but kept for future use (e.g. timeouts).
        (void)dt;

        // For each agent, if they are idle, try to assign the best job.
        for (AgentId agent : agents_)
        {
            if (!agentAdapter_.isAgentIdle(agent))
                continue;

            const Int2 agentTile = agentAdapter_.getAgentTile(agent);

            // Use priority minus Manhattan distance as a simple score.
            Job* bestJob = queue_.acquireBestJob(
                [&](const Job& job) -> float
                {
                    if (job.state != JobState::Open)
                        return -1.0e9f; // effectively "impossible"

                    const int dx = job.targetTile.x - agentTile.x;
                    const int dy = job.targetTile.y - agentTile.y;
                    const float distance = static_cast<float>(std::abs(dx) + std::abs(dy));

                    // Higher priority and closer distance give higher score.
                    // You can tweak this formula however you like.
                    return static_cast<float>(job.priority) - distance;
                });

            if (bestJob != nullptr)
            {
                bestJob->state = JobState::InProgress;
                bestJob->assignedAgent = agent;

                agentAdapter_.assignJobToAgent(agent, *bestJob);
            }
        }
    }

} // namespace colony::jobs
