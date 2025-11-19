#include "jobs/JobSystem.h"

#include <algorithm> // std::find, std::find_if, std::remove
#include <cmath>     // std::abs
#include <limits>    // std::numeric_limits

namespace colony::jobs
{

// -----------------------------------------------------------------------------
// Singleton boilerplate
// -----------------------------------------------------------------------------

JobSystem& JobSystem::Instance()
{
    static JobSystem instance;
    return instance;
}

JobSystem::JobSystem() = default;
JobSystem::~JobSystem() = default;

// -----------------------------------------------------------------------------
// Internal helper
// -----------------------------------------------------------------------------

Job* JobSystem::findJob(JobId id)
{
    auto it = std::find_if(queue_.begin(), queue_.end(),
        [id](const Job& job) { return job.id == id; });

    return (it != queue_.end()) ? &(*it) : nullptr;
}

// -----------------------------------------------------------------------------
// Public gameplay job API
// -----------------------------------------------------------------------------

JobId JobSystem::createJob(JobType type, const Int2& targetTile, int priority)
{
    Job job{}; // value-initialize to avoid any uninitialized fields
    job.id            = nextJobId_++;
    job.type          = type;
    job.targetTile    = targetTile;
    job.priority      = priority;
    job.state         = JobState::Open;
    job.assignedAgent = 0;  // 0 = no agent assigned

    queue_.push_back(job);
    return job.id;
}

void JobSystem::notifyJobCompleted(JobId jobId, AgentId agent)
{
    Job* job = findJob(jobId);
    if (!job)
        return;

    // Optional safety: ignore if some other agent calls this.
    if (job->assignedAgent != 0 && job->assignedAgent != agent)
        return;

    job->state         = JobState::Completed;
    job->assignedAgent = 0;
}

void JobSystem::notifyJobFailed(JobId jobId, AgentId agent)
{
    Job* job = findJob(jobId);
    if (!job)
        return;

    // Optional safety: ignore if some other agent calls this.
    if (job->assignedAgent != 0 && job->assignedAgent != agent)
        return;

    // Re-open the job for someone else to try.
    job->state         = JobState::Open;
    job->assignedAgent = 0;
}

void JobSystem::registerAgent(AgentId agent)
{
    const auto it = std::find(agents_.begin(), agents_.end(), agent);
    if (it == agents_.end())
    {
        agents_.push_back(agent);
    }
}

void JobSystem::unregisterAgent(AgentId agent)
{
    const auto it = std::remove(agents_.begin(), agents_.end(), agent);
    if (it != agents_.end())
    {
        agents_.erase(it, agents_.end());
    }

    // Optionally clear jobs assigned to this agent.
    for (Job& job : queue_)
    {
        if (job.assignedAgent == agent && job.state == JobState::InProgress)
        {
            job.assignedAgent = 0;
            job.state         = JobState::Open;
        }
    }
}

void JobSystem::update(float dt)
{
    // dt is currently unused but kept for future use (e.g. timeouts).
    (void)dt;

    // If the adapter hasn't been set up yet, do nothing.
    if (!agentAdapter_)
        return;

    // For each registered agent, if they are idle, try to assign the best job.
    for (AgentId agent : agents_)
    {
        if (!agentAdapter_->isAgentIdle(agent))
            continue;

        const Int2 agentTile = agentAdapter_->getAgentTile(agent);

        Job*  bestJob   = nullptr;
        float bestScore = std::numeric_limits<float>::lowest();

        for (Job& job : queue_)
        {
            if (job.state != JobState::Open)
                continue;

            const int   dx      = job.targetTile.x - agentTile.x;
            const int   dy      = job.targetTile.y - agentTile.y;
            const float dist    = static_cast<float>(std::abs(dx) + std::abs(dy));
            const float score   = static_cast<float>(job.priority) - dist;

            if (!bestJob || score > bestScore)
            {
                bestScore = score;
                bestJob   = &job;
            }
        }

        if (bestJob != nullptr)
        {
            bestJob->state         = JobState::InProgress;
            bestJob->assignedAgent = agent;

            agentAdapter_->assignJobToAgent(agent, *bestJob);
        }
    }
}

} // namespace colony::jobs
