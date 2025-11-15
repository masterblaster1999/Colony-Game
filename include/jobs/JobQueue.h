#pragma once

#include <vector>
#include <limits>
#include "jobs/Job.h"

namespace colony::jobs
{
    // Owns all current jobs and basic operations to create / cancel / find them.
    class JobQueue
    {
    public:
        JobQueue();

        // Create a new job and return its ID.
        JobId addJob(JobType type, const Int2& targetTile, int priority);

        // Mark a job as cancelled. Returns false if not found or already completed/cancelled.
        bool cancelJob(JobId id);

        // Lookup helpers (linear search for simplicity; fine for small N).
        Job*       getJob(JobId id);
        const Job* getJob(JobId id) const;

        // Clear all jobs.
        void clear();

        // Access to raw storage (for debug, saving, etc.).
        const std::vector<Job>& jobs() const { return jobs_; }

        // Acquire the "best" open job based on a scoring function.
        // ScoreFn : float(const Job&)
        // Higher scores are better. Returns nullptr if nothing suitable.
        template <typename ScoreFn>
        Job* acquireBestJob(ScoreFn scorer)
        {
            float bestScore = std::numeric_limits<float>::lowest();
            Job*  bestJob   = nullptr;

            for (Job& job : jobs_)
            {
                if (job.state != JobState::Open)
                    continue;

                float score = scorer(job);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestJob   = &job;
                }
            }

            if (bestJob != nullptr)
            {
                bestJob->state = JobState::Reserved;
            }

            return bestJob;
        }

    private:
        JobId              nextJobId_ = 1; // 0 reserved as invalid
        std::vector<Job>   jobs_;
    };

} // namespace colony::jobs
