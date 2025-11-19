#pragma once

// Taskflow core and algorithms
#include <taskflow/taskflow.hpp>                  // tf::Executor, tf::Taskflow, tf::Future
#include <taskflow/algorithm/for_each.hpp>        // tf::Taskflow::for_each, for_each_index

// STL
#include <type_traits>
#include <utility>
#include <thread>
#include <vector>
#include <cstdint>

// -----------------------------------------------------------------------------
// Gameplay job / agent types (Step 1 patch)
// -----------------------------------------------------------------------------

// Forward declaration: implemented in your gameplay code.
class IAgentAdapter;

// Simple identifier types for agents and jobs.
using AgentId = std::uint32_t;
using JobId   = std::uint32_t;

// Basic job state for colony-style jobs.
enum class JobState
{
  Open,        // waiting to be picked by an agent
  InProgress,  // currently assigned to an agent
  Completed,   // finished successfully
  Failed       // failed (optional, for retries / UI)
};

// Minimal job representation; extend as needed in your .cpp / game logic.
struct Job
{
  JobId    id        = 0;
  JobState state     = JobState::Open;
  // Add fields as needed, e.g. target tile, category, priority, etc.
  // Example (uncomment/adapt to your types):
  // TileCoord targetTile{};
  // float     basePriority = 0.0f;
};

// -----------------------------------------------------------------------------
// JobSystem
// -----------------------------------------------------------------------------

class JobSystem {
public:
  // Singleton access (defined in JobSystem.cpp)
  static JobSystem& Instance();

  // ---------------------------------------------------------------------------
  // Taskflow executor API (existing code, unchanged)
  // ---------------------------------------------------------------------------

  // Access to the underlying executor
  tf::Executor& executor() noexcept { return _executor; }
  const tf::Executor& executor() const noexcept { return _executor; }

  // Launch a callable asynchronously on the executor.
  // Returns std::future<R> (per Taskflow API).
  // https://taskflow.github.io/taskflow/AsyncTasking.html
  template <typename F>
  auto Async(F&& f) {
    return _executor.async(std::forward<F>(f));
  }

  // Iterator-based parallel for_each over [first, last)
  // Non-blocking: returns tf::Future<void> from executor.run(...)
  template <typename It, typename F>
  tf::Future<void> ParallelForAsync(It first, It last, F&& fn) {
    tf::Taskflow taskflow;
    taskflow.for_each(first, last, std::forward<F>(fn));
    return _executor.run(std::move(taskflow));    // tf::Future<void>
  }

  // Iterator-based parallel for_each with an explicit partitioner
  // (e.g., tf::StaticPartitioner, tf::GuidedPartitioner).
  // See: https://taskflow.github.io/taskflow/ParallelIterations.html
  template <typename It, typename F, typename Partitioner>
  tf::Future<void> ParallelForAsync(It first, It last, F&& fn, Partitioner&& part) {
    tf::Taskflow taskflow;
    taskflow.for_each(first, last, std::forward<F>(fn), std::forward<Partitioner>(part));
    return _executor.run(std::move(taskflow));
  }

  // Index-based parallel for_each_index over [first, last) with step.
  // Works for integral indices; non-blocking like above.
  template <typename Index, typename F>
  std::enable_if_t<std::is_integral_v<Index>, tf::Future<void>>
  ParallelForIndexAsync(Index first, Index last, Index step, F&& fn) {
    tf::Taskflow taskflow;
    taskflow.for_each_index(first, last, step, std::forward<F>(fn));
    return _executor.run(std::move(taskflow));
  }

  // -------- Optional conveniences --------

  // Blocking variants for external threads (e.g., your main/game thread).
  // Do NOT call these from inside a task running on this executor; prefer Corun(...)
  // to avoid deadlock patterns described in the docs.
  template <typename It, typename F>
  void ParallelFor(It first, It last, F&& fn) {
    ParallelForAsync(first, last, std::forward<F>(fn)).wait();
  }

  template <typename Index, typename F>
  std::enable_if_t<std::is_integral_v<Index>, void>
  ParallelForIndex(Index first, Index last, Index step, F&& fn) {
    ParallelForIndexAsync(first, last, step, std::forward<F>(fn)).wait();
  }

  // Run a pre-built taskflow (non-blocking)
  tf::Future<void> Run(tf::Taskflow&& tfw) {
    return _executor.run(std::move(tfw));
  }

  // Wait for all outstanding work (safe from external threads).
  void WaitAll() { _executor.wait_for_all(); }

  // Cooperative run: call this from *inside* a task to join another taskflow
  // without blocking the worker thread (prevents deadlock). See docs:
  // https://taskflow.github.io/taskflow/classtf_1_1Executor.html#details
  void Corun(tf::Taskflow& tfw) { _executor.corun(tfw); }

  // Discover hardware threads (useful for sizing).
  static unsigned Concurrency() noexcept { return std::thread::hardware_concurrency(); }

  // ---------------------------------------------------------------------------
  // Gameplay job-system API (Step 1 patch)
  // ---------------------------------------------------------------------------

  // Provide the adapter used to talk to your in-game agents/colonists.
  // Call this once at startup before using the job API.
  void InitializeAgentAdapter(IAgentAdapter& adapter) noexcept {
    _agentAdapter = &adapter;
  }

  // Create a new gameplay job based on a template and return its id.
  // Implementation in JobSystem.cpp can fill in id, position, etc.
  JobId createJob(const Job& jobTemplate);

  // Notify the system that a job has completed or failed.
  void notifyJobCompleted(JobId jobId);
  void notifyJobFailed(JobId jobId);

  // Register/unregister agents that are allowed to receive jobs.
  void registerAgent(AgentId agentId);
  void unregisterAgent(AgentId agentId);

  // Per-frame update to assign jobs to agents, update states, etc.
  void update(float dt);

private:
  JobSystem();                      // defined in JobSystem.cpp
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  // Existing Taskflow executor
  tf::Executor _executor;

  // Gameplay job-system state (Step 1 patch)
  IAgentAdapter*      _agentAdapter = nullptr;
  std::vector<Job>    queue_;       // job queue
  std::vector<AgentId> agents_;     // registered agents
  JobId               nextJobId_ = 1;
};
