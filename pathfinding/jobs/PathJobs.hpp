#pragma once
// PathJobs.hpp - header-only Taskflow pathfinding job runner
//
// Drop into: pathfinding/jobs/PathJobs.hpp
// Requires:  taskflow (header-only). In your code: #include <taskflow/taskflow.hpp>
// MSVC: /std:c++20 (or newer). No platform-specific calls; Windows-only project OK.

#include <taskflow/taskflow.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace colony::pathjobs {

// ------------------------------
// Basic types (keep these minimal; adapt to your grid/world types)
// ------------------------------

using JobId = std::uint64_t;

// Opaque ID you can map to an ECS entity/colonist/agent.
// Keep it POD so we don't drag EnTT into this header.
using AgentId = std::uint32_t;

struct GridPos {
  int x{};
  int y{};
};

enum class PathStatus : std::uint8_t {
  Queued,
  Running,
  Succeeded,
  NotFound,
  Failed,
  Cancelled
};

struct PathRequest {
  AgentId agent{0};
  GridPos start{};
  GridPos goal{};
  bool allow_diagonals{false};

  // Optional deadline for "give up" (cooperative; your pathfinder must check it).
  std::optional<std::chrono::steady_clock::time_point> deadline{};

  // Optional user tag you can use to correlate with gameplay systems.
  std::uint32_t user_tag{0};

  // Optional per-request cost/heuristic scaling
  float heuristic_weight{1.0f};
};

// Result object your pathfinder returns to the runner.
struct PathResult {
  JobId     id{};
  AgentId   agent{0};
  PathStatus status{PathStatus::Failed};
  std::vector<GridPos> path;    // empty if NotFound/Failed/Cancelled
  float     total_cost{0.0f};
  std::string error;            // developer-facing message on failure
};

// Cooperative cancellation token (shared across threads).
struct CancelToken {
  std::atomic<bool> cancelled{false};
  void cancel() noexcept { cancelled.store(true, std::memory_order_relaxed); }
  bool is_cancelled() const noexcept { return cancelled.load(std::memory_order_relaxed); }
};

// Your pathfinder signature. Keep it generic.
// Pass the cancel token by reference so the algorithm can check periodically.
using PathfinderFn = std::function<PathResult(const PathRequest&, CancelToken&)>;

// ------------------------------
// PathJobRunner
// ------------------------------

class PathJobRunner {
public:
  struct Config {
    // Number of worker threads in the Taskflow executor.
    // If 0, we auto-pick max(1, hw_concurrency-2).
    unsigned worker_threads{0};

    // Optional max jobs tracked concurrently (prevents unbounded growth of the job map).
    // 0 means unlimited.
    std::size_t max_tracked_jobs{0};
  };

  explicit PathJobRunner(PathfinderFn pathfinder, Config cfg = {})
  : _pathfinder(std::move(pathfinder))
  , _executor(resolve_worker_count(cfg.worker_threads))
  , _max_tracked(cfg.max_tracked_jobs) {}

  // Non-copyable, movable if you need it.
  PathJobRunner(const PathJobRunner&) = delete;
  PathJobRunner& operator=(const PathJobRunner&) = delete;
  PathJobRunner(PathJobRunner&&) = default;
  PathJobRunner& operator=(PathJobRunner&&) = default;

  ~PathJobRunner() {
    // Best-effort cooperative shutdown:
    //  - mark all tokens cancelled so algorithms can early-out
    //  - we don't block here; destructor of executor waits for all tasks to finish
    //    once user code drops all futures (we own them in _jobs).
    std::lock_guard<std::mutex> lk(_mx);
    for (auto& [_, j] : _jobs) {
      j.token->cancel();
    }
  }

  // Submit a single path request. Returns a JobId you can use to cancel or query.
  JobId submit(const PathRequest& req) {
    ensure_pathfinder_();

    // Guard against unbounded growth if the user configured a cap.
    if (_max_tracked != 0) {
      std::lock_guard<std::mutex> lk(_mx);
      if (_jobs.size() >= _max_tracked) {
        // You can choose to throw, drop, or return 0. We return 0 to signal failure to enqueue.
        return 0;
      }
    }

    JobId id = ++_seq;

    auto token = std::make_shared<CancelToken>();

    // Launch the pathfinder asynchronously on the executor thread pool.
    // Returns a std::future<PathResult>. (Taskflow async doc)  ✅
    // https://taskflow.github.io/taskflow/AsyncTasking.html
    auto fut = _executor.async([this, req, token, id]() -> PathResult {
      PathRequest rq = req; // copy; safe to mutate if needed in pathfinder
      PathResult   rr = _pathfinder(rq, *token);
      rr.id = id;
      return rr;
    });

    // Track it.
    {
      std::lock_guard<std::mutex> lk(_mx);
      _jobs.emplace(id, Job{std::move(fut), std::move(token), req.agent});
    }

    return id;
  }

  // Bulk submit: reserves IDs and returns them in the same order as input.
  template <typename InputIt>
  std::vector<JobId> submit_bulk(InputIt first, InputIt last) {
    std::vector<JobId> ids;
    ids.reserve(static_cast<std::size_t>(std::distance(first, last)));
    for (auto it = first; it != last; ++it) {
      ids.push_back(submit(*it));
    }
    return ids;
  }

  // Cooperative cancel. The running pathfinder must check the token to exit quickly.
  bool cancel(JobId id) {
    std::lock_guard<std::mutex> lk(_mx);
    auto it = _jobs.find(id);
    if (it == _jobs.end()) return false;
    it->second.token->cancel();
    return true;
  }

  // Poll for completed results (non-blocking).
  // Collect up to max_to_collect results (0 = all ready).
  std::vector<PathResult> poll(std::size_t max_to_collect = 0) {
    std::vector<JobId> ready_ids;
    ready_ids.reserve(32);

    {
      std::lock_guard<std::mutex> lk(_mx);
      for (auto& [id, j] : _jobs) {
        using namespace std::chrono_literals;
        if (j.future.wait_for(0ms) == std::future_status::ready) {
          ready_ids.push_back(id);
          if (max_to_collect != 0 && ready_ids.size() >= max_to_collect) break;
        }
      }
    }

    std::vector<PathResult> results;
    results.reserve(ready_ids.size());

    for (JobId id : ready_ids) {
      std::future<PathResult> fut;
      {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _jobs.find(id);
        if (it == _jobs.end()) continue;
        fut = std::move(it->second.future);
        _jobs.erase(it);
      }
      // get() outside the lock to avoid holding the mutex while potentially blocking
      results.push_back(fut.get());
    }

    return results;
  }

  // Blocking collect for a specific job (optional convenience).
  std::optional<PathResult> wait(JobId id) {
    std::future<PathResult> fut;
    {
      std::lock_guard<std::mutex> lk(_mx);
      auto it = _jobs.find(id);
      if (it == _jobs.end()) return std::nullopt;
      fut = std::move(it->second.future);
      _jobs.erase(it);
    }
    return fut.get();
  }

  // How many jobs are currently tracked (in-flight or not yet collected).
  std::size_t tracked_jobs() const {
    std::lock_guard<std::mutex> lk(_mx);
    return _jobs.size();
  }

  unsigned worker_count() const noexcept {
    return _executor.num_workers();
  }

private:
  struct Job {
    std::future<PathResult> future;
    std::shared_ptr<CancelToken> token;
    AgentId agent;
  };

  static unsigned resolve_worker_count(unsigned requested) {
    if (requested > 0) return requested;
    // Default: leave 2 cores for the main/render threads; always >= 1.
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned base = hw > 2 ? (hw - 2) : 1u;
    // Per Taskflow docs, worker count must be > 0, or executor throws. ✅
    // https://taskflow.github.io/taskflow/classtf_1_1Executor.html
    return base;
  }

  void ensure_pathfinder_() const {
    if (!_pathfinder) {
      throw std::runtime_error("PathJobRunner: pathfinder function not set");
    }
  }

  // State
  PathfinderFn _pathfinder;
  tf::Executor _executor;
  std::size_t  _max_tracked{0};

  mutable std::mutex _mx;
  std::unordered_map<JobId, Job> _jobs;
  std::atomic<JobId> _seq{0};
};

} // namespace colony::pathjobs
