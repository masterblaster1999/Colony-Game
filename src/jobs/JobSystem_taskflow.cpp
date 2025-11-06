// src/jobs/JobSystem_taskflow.cpp
//
// This translation unit intentionally contains no definitions.
// The JobSystem public API is provided inline in JobSystem.h,
// and the non-inline parts (Instance(), ctor/dtor) are implemented
// in JobSystem.cpp. Keeping this TU preserves the original project
// structure without reintroducing the legacy API surface.
//
// Previous (legacy) symbols removed here:
//   - JobSystem::init(unsigned)
//   - JobSystem::shutdown()
//   - JobSystem::parallel_for(JobRange, uint32_t, const std::function<void(JobRange)>&)
//   - JobSystem::wait_all()
//
// Use the current API instead (see JobSystem.h):
//   - JobSystem::Instance()
//   - JobSystem::executor()
//   - JobSystem::ParallelFor / ParallelForIndex (blocking)
//   - JobSystem::ParallelForAsync / ParallelForIndexAsync (non-blocking)
//   - JobSystem::Run / WaitAll / Corun
//
// Header:   src/jobs/JobSystem.h
// Impl:     src/jobs/JobSystem.cpp

#include "jobs/JobSystem.h"
