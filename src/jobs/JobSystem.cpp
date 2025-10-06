#include "JobSystem.h"

JobSystem& JobSystem::Instance() {
  static JobSystem instance;
  return instance;
}

JobSystem::JobSystem()
: _executor(std::max(1u, std::thread::hardware_concurrency() > 0
                        ? std::thread::hardware_concurrency() - 1u
                        : 1u)) {}

JobSystem::~JobSystem() = default;
