#pragma once

// Taskflow core and algorithms
#include <taskflow/taskflow.hpp>                  // tf::Executor, tf::Taskflow, tf::Future
#include <taskflow/algorithm/for_each.hpp>        // tf::Taskflow::for_each, for_each_index

// STL
#include <type_traits>
#include <utility>
#include <thread>

class JobSystem {
public:
  // Singleton access (defined in JobSystem.cpp)
  static JobSystem& Instance();

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

private:
  JobSystem();                      // defined in JobSystem.cpp
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  tf::Executor _executor;
};
