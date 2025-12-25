#include "loop/FramePacingStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace colony::appwin {

namespace {
    double SafeFpsFromMs(double ms) noexcept
    {
        return (ms > 0.0) ? (1000.0 / ms) : 0.0;
    }

    template <typename Vec>
    double Average(const Vec& v) noexcept
    {
        if (v.empty()) return 0.0;
        const double sum = std::accumulate(v.begin(), v.end(), 0.0);
        return sum / static_cast<double>(v.size());
    }
}

FramePacingStats::FramePacingStats(std::size_t capacity)
{
    if (capacity < 30) capacity = 30;
    if (capacity > 2000) capacity = 2000;

    m_capacity = capacity;
    m_frameMs.assign(m_capacity, 0.0);
    m_presentMs.assign(m_capacity, 0.0);
    m_waitMs.assign(m_capacity, 0.0);
    m_sortedFrameMs.reserve(m_capacity);
}

void FramePacingStats::Reset() noexcept
{
    m_index = 0;
    m_count = 0;
    m_summary = FramePacingSummary{};
    m_hasLastCompute = false;
}

void FramePacingStats::AddSample(double frameMs, double presentMs, double waitMs) noexcept
{
    if (m_capacity == 0)
        return;

    m_frameMs[m_index] = frameMs;
    m_presentMs[m_index] = presentMs;
    m_waitMs[m_index] = waitMs;

    m_index = (m_index + 1) % m_capacity;
    if (m_count < m_capacity)
        ++m_count;
}

bool FramePacingStats::Update(std::chrono::steady_clock::time_point now,
                              std::chrono::milliseconds minInterval) noexcept
{
    if (!m_hasLastCompute)
    {
        m_lastCompute = now;
        m_hasLastCompute = true;
        RecomputeSummary();
        return true;
    }

    if (now - m_lastCompute >= minInterval)
    {
        m_lastCompute = now;
        RecomputeSummary();
        return true;
    }

    return false;
}

double FramePacingStats::PercentileNearestRank(const std::vector<double>& sorted, double pct01) noexcept
{
    if (sorted.empty())
        return 0.0;

    if (pct01 <= 0.0) return sorted.front();
    if (pct01 >= 1.0) return sorted.back();

    // Nearest-rank style selection.
    const double f = pct01 * static_cast<double>(sorted.size() - 1);
    const std::size_t idx = static_cast<std::size_t>(std::llround(f));
    return sorted[std::min<std::size_t>(idx, sorted.size() - 1)];
}

void FramePacingStats::RecomputeSummary() noexcept
{
    FramePacingSummary s{};
    s.sampleCount = m_count;
    if (m_count == 0)
    {
        m_summary = s;
        return;
    }

    // Extract samples from ring into scratch and sort.
    m_sortedFrameMs.clear();
    m_sortedFrameMs.reserve(m_count);

    // Oldest sample is at m_index when full; if not full, start at 0.
    const std::size_t start = (m_count == m_capacity) ? m_index : 0;
    for (std::size_t i = 0; i < m_count; ++i)
    {
        const std::size_t idx = (start + i) % m_capacity;
        m_sortedFrameMs.push_back(m_frameMs[idx]);
    }

    std::sort(m_sortedFrameMs.begin(), m_sortedFrameMs.end());

    // Avg/min/max.
    s.minMs = m_sortedFrameMs.front();
    s.maxMs = m_sortedFrameMs.back();
    s.avgMs = Average(m_sortedFrameMs);
    s.fps = SafeFpsFromMs(s.avgMs);

    // Percentiles.
    s.p50Ms = PercentileNearestRank(m_sortedFrameMs, 0.50);
    s.p95Ms = PercentileNearestRank(m_sortedFrameMs, 0.95);
    s.p99Ms = PercentileNearestRank(m_sortedFrameMs, 0.99);

    // Low FPS metrics (average of worst X% frame times).
    const std::size_t worst1Count = std::max<std::size_t>(1, (m_count + 99) / 100);     // ceil(count * 0.01)
    const std::size_t worst01Count = std::max<std::size_t>(1, (m_count + 999) / 1000);  // ceil(count * 0.001)

    const auto worst1Begin = m_sortedFrameMs.end() - static_cast<std::ptrdiff_t>(worst1Count);
    const auto worst01Begin = m_sortedFrameMs.end() - static_cast<std::ptrdiff_t>(worst01Count);

    const double worst1AvgMs = std::accumulate(worst1Begin, m_sortedFrameMs.end(), 0.0) / double(worst1Count);
    const double worst01AvgMs = std::accumulate(worst01Begin, m_sortedFrameMs.end(), 0.0) / double(worst01Count);

    s.onePercentLowFps = SafeFpsFromMs(worst1AvgMs);
    s.pointOnePercentLowFps = SafeFpsFromMs(worst01AvgMs);

    // Averages for Present() / wait time (no sorting needed).
    // Use same (oldest->newest) window as the frame times above.
    double sumPresent = 0.0;
    double sumWait = 0.0;
    for (std::size_t i = 0; i < m_count; ++i)
    {
        const std::size_t idx = (start + i) % m_capacity;
        sumPresent += m_presentMs[idx];
        sumWait += m_waitMs[idx];
    }
    s.avgPresentMs = sumPresent / double(m_count);
    s.avgWaitMs = sumWait / double(m_count);

    m_summary = s;
}

std::wstring FramePacingStats::FormatTitleString() const
{
    const auto& s = m_summary;
    if (s.sampleCount == 0)
        return L"(no samples)";

    std::wostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);

    oss << L"FT "
        << s.avgMs << L"ms"
        << L" p95 " << s.p95Ms << L"ms"
        << L" p99 " << s.p99Ms << L"ms";

    oss.precision(0);
    oss << L" | 1% " << s.onePercentLowFps << L"fps"
        << L" 0.1% " << s.pointOnePercentLowFps << L"fps";

    oss.precision(2);
    oss << L" | wait " << s.avgWaitMs << L"ms"
        << L" pres " << s.avgPresentMs << L"ms";

    return oss.str();
}

} // namespace colony::appwin
