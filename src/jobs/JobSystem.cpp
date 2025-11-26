#include "jobs/JobSystem.h"

#include <algorithm> // std::find, std::find_if, std::remove
#include <cmath>     // std::abs
#include <limits>    // std::numeric_limits
#include <objbase.h> // CoInitializeEx, CoUninitialize

#pragma comment(lib, "ole32.lib") // CoInitializeEx/CoUninitialize import lib

// -----------------------------------------------------------------------------
// Per-thread COM initialization (RAII) for WIC/DirectXTex users.
// Any thread that may decode/encode images must initialize COM.
// This helper guarantees one CoInitializeEx per thread and pairs it with
// CoUninitialize only when appropriate (S_OK/S_FALSE).
// -----------------------------------------------------------------------------
namespace {
struct ComInitScope {
    HRESULT hr = E_FAIL;
    explicit ComInitScope(DWORD flags = COINIT_MULTITHREADED) noexcept {
        hr = ::CoInitializeEx(nullptr, flags);
        // Accept S_OK (fresh), S_FALSE (already initialized on this thread).
        // If RPC_E_CHANGED_MODE is returned, don't call CoUninitialize later.
    }
    ~ComInitScope() noexcept {
        if (hr == S_OK || hr == S_FALSE) {
            ::CoUninitialize();
        }
    }
};

// Ensure COM is initialized on the current thread exactly once.
// Call at the top of any API that can run on worker threads which touch WIC/DirectXTex.
inline void EnsureCom() noexcept {
    static thread_local ComInitScope s_com(COINIT_MULTITHREADED);
    (void)s_com;
}
} // namespace

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
    EnsureCom(); // safe no-op if already initialized on this thread

    // Linear search through the job queue by ID.
    // Returns nullptr if the job doesn't exist.
    auto it = std::find_if(
        queue_.begin(),
        queue_.end(),
        [id](const Job& job)
        {
            return job.id == id;
        }
    );

    return (it != queue_.end()) ? &(*it) : nullptr;
}

// -----------------------------------------------------------------------------
// Public gameplay job API
// -----------------------------------------------------------------------------

JobId JobSystem::createJob(JobType type, const Int2& targetTile, int priority)
{
    EnsureCom(); // workers that create jobs (IO, streaming) will be COM-initialized

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
    EnsureCom();

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
    EnsureCom();

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
    EnsureCom();

    const auto it = std::find(agents_.begin(), agents_.end(), agent);
    if (it == agents_.end())
    {
        agents_.push_back(agent);
    }
}

void JobSystem::unregisterAgent(AgentId agent)
{
    EnsureCom();

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
    EnsureCom();

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

            const int   dx   = job.targetTile.x - agentTile.x;
            const int   dy   = job.targetTile.y - agentTile.y;
            const float dist = static_cast<float>(std::abs(dx) + std::abs(dy));
            const float score =
                static_cast<float>(job.priority) - dist; // simple priority - distance heuristic

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
