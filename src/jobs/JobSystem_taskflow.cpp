// src/jobs/JobSystem_taskflow.cpp
#include "jobs/JobSystem.h"
#include <taskflow/taskflow.hpp>

static tf::Executor g_exec;

void JobSystem::init(unsigned){ /* executor picks threads automatically */ }
void JobSystem::shutdown(){ g_exec.wait_for_all(); }

void JobSystem::parallel_for(JobRange r, uint32_t grain, const std::function<void(JobRange)>& fn) {
  tf::Taskflow tf;  // short-lived graph per call is fine; or keep one per frame
  tf.for_each_index(r.begin, r.end, int(grain), [&](int i){ fn({ uint32_t(i), std::min<uint32_t>(i+grain, r.end) }); });
  g_exec.run(tf).wait();
}

void JobSystem::wait_all(){ g_exec.wait_for_all(); }
