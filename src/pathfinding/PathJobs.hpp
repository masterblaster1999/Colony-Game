#pragma once
#include <future>
#include <vector>
#include <cstdint>
#include <utility>
#include "../common/ThreadPool.hpp"

namespace colony::path {

// Minimal PODs you can adapt to your project types:
struct GridPos { int x{}, y{}; };

struct PathRequest {
    GridPos start;
    GridPos goal;
    std::uint32_t mask = 0; // e.g., walkability layers/flags
};

struct PathResult {
    bool success = false;
    std::vector<GridPos> waypoints;
};

// The user supplies the actual path computation (A*, JPS, HPA*, etc.).
using ComputePathFn = PathResult(*)(const PathRequest&);

// Enqueue a pathfinding job; returns a future<PathResult>.
inline std::future<PathResult>
SubmitPathJob(colony::ThreadPool& pool, PathRequest req, ComputePathFn fn) {
    return pool.submit([req, fn]() {
        return fn(req);
    });
}

} // namespace colony::path
