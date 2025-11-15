#include "jobs/JobQueue.h"

namespace colony::jobs
{
    JobQueue::JobQueue() = default;

    JobId JobQueue::addJob(JobType type, const Int2& targetTile, int priority)
    {
        Job job;
        job.id       = nextJobId_++;
        job.type     = type;
        job.state    = JobState::Open;
        job.priority = priority;
        job.targetTile = targetTile;
        job.assignedAgent = 0;

        jobs_.push_back(job);
        return job.id;
    }

    bool JobQueue::cancelJob(JobId id)
    {
        for (Job& job : jobs_)
        {
            if (job.id == id)
            {
                if (job.state == JobState::Completed || job.state == JobState::Cancelled)
                    return false;

                job.state = JobState::Cancelled;
                job.assignedAgent = 0;
                return true;
            }
        }
        return false;
    }

    Job* JobQueue::getJob(JobId id)
    {
        for (Job& job : jobs_)
        {
            if (job.id == id)
                return &job;
        }
        return nullptr;
    }

    const Job* JobQueue::getJob(JobId id) const
    {
        for (const Job& job : jobs_)
        {
            if (job.id == id)
                return &job;
        }
        return nullptr;
    }

    void JobQueue::clear()
    {
        jobs_.clear();
        nextJobId_ = 1;
    }

} // namespace colony::jobs
