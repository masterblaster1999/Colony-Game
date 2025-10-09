#pragma once
#include <cstdint>
#include <algorithm>

namespace colony::sim {

// Result of advancing the fixed-step accumulator in one rendered frame.
struct TickResult {
    int    steps;   // how many fixed updates to run this frame
    double alpha;   // interpolation factor for rendering [0,1)
};

// Glenn Fiedler's canonical fixed-timestep accumulator pattern, adapted for C++.
// See: "Fix Your Timestep!" (Gaffer on Games). :contentReference[oaicite:1]{index=1}
class FixedTimestep {
public:
    explicit FixedTimestep(double dtSeconds = 1.0 / 60.0, double maxFrameClamp = 0.25)
        : m_dt(dtSeconds), m_maxFrameClamp(maxFrameClamp) {}

    // Add one variable-length frame of time; returns how many fixed steps to run now.
    template <class UpdateFn>
    TickResult step(double frameSeconds, UpdateFn&& onFixedUpdate)
    {
        frameSeconds = std::min(frameSeconds, m_maxFrameClamp);
        m_accum += frameSeconds;
        int steps = 0;
        while (m_accum >= m_dt) {
            onFixedUpdate(m_dt, m_tick++);
            m_accum -= m_dt;
            ++steps;
        }
        return { steps, m_accum / m_dt };
    }

    [[nodiscard]] double   dt()    const { return m_dt; }
    [[nodiscard]] uint64_t ticks() const { return m_tick; }

private:
    double   m_dt;
    double   m_maxFrameClamp;
    double   m_accum{0.0};
    uint64_t m_tick{0};
};

} // namespace colony::sim
