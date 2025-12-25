#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace colony::appwin {

struct FramePacingSummary
{
    std::size_t sampleCount = 0;

    double fps = 0.0; // 1000 / avgMs
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;

    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;

    // Common "PresentMon-style" low metrics (computed as average of worst X% frame times).
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;

    double avgPresentMs = 0.0; // CPU time spent inside Present()
    double avgWaitMs = 0.0;    // CPU time spent waiting for the frame latency object
};

// Small, dependency-free frame pacing tracker.
//
// This is not a full PresentMon ETW implementation (that would require ETW + DXGI present events),
// but it provides a very useful in-app approximation for tuning:
//  - frame time percentiles (p50/p95/p99)
//  - 1% low / 0.1% low FPS
//  - avg Present() call time + avg wait time
class FramePacingStats
{
public:
    explicit FramePacingStats(std::size_t capacity = 240);

    void Reset() noexcept;

    void AddSample(double frameMs, double presentMs, double waitMs) noexcept;

    // Recompute summary at most every `minInterval`. Returns true if recomputed.
    bool Update(std::chrono::steady_clock::time_point now,
                std::chrono::milliseconds minInterval = std::chrono::milliseconds(500)) noexcept;

    [[nodiscard]] const FramePacingSummary& Summary() const noexcept { return m_summary; }

    // Compact summary intended for window titles / logs.
    [[nodiscard]] std::wstring FormatTitleString() const;

private:
    void RecomputeSummary() noexcept;

    static double PercentileNearestRank(const std::vector<double>& sorted, double pct01) noexcept;

    std::size_t m_capacity = 240;
    std::size_t m_index = 0;
    std::size_t m_count = 0;

    // Ring buffers (size == m_capacity)
    std::vector<double> m_frameMs;
    std::vector<double> m_presentMs;
    std::vector<double> m_waitMs;

    // Scratch buffers reused for summary computation (avoid reallocation)
    std::vector<double> m_sortedFrameMs;

    FramePacingSummary m_summary{};

    std::chrono::steady_clock::time_point m_lastCompute{};
    bool m_hasLastCompute = false;
};

} // namespace colony::appwin
