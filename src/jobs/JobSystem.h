#pragma once
#include <taskflow/taskflow.hpp>
#include <algorithm>
#include <thread>

class JobSystem {
public:
  static JobSystem& Instance();

  tf::Executor& executor() { return _executor; }

  template <typename F>
  auto Async(F&& f) {
    return _executor.async(std::forward<F>(f));
  }

  template <typename It, typename F>
  tf::Future ParallelForAsync(It first, It last, F&& fn) {
    tf::Taskflow tf;
    tf.for_each(first, last, std::forward<F>(fn));
    return _executor.run(std::move(tf));
  }

private:
  JobSystem();
  ~JobSystem();
  tf::Executor _executor;
};
