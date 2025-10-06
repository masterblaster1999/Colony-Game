#pragma once
#include <cstdint>
#include <chrono>

// A tiny fixed-step helper for ~60 Hz simulation. Replace with your own if you already have one.
namespace core
{
    class FixedTimestep
    {
    public:
        explicit FixedTimestep(double hz = 60.0)
            : m_dt(1.0 / hz) {}

        // Returns the number of sim updates to run this frame and the interpolation alpha [0,1).
        std::pair<int, double> BeginFrame()
        {
            using clock = std::chrono::high_resolution_clock;
            const auto now = clock::now();
            const double frame = m_initialized ? std::chrono::duration<double>(now - m_prev).count() : m_dt;
            m_prev = now; m_initialized = true;

            // Clamp to avoid spiral of death if minimized/paused
            double clamped = frame > 0.25 ? 0.25 : frame;
            m_accum += clamped;

            int steps = 0;
            while (m_accum + 1e-9 >= m_dt && steps < m_maxSteps)
            {
                ++steps;
                m_accum -= m_dt;
            }
            const double alpha = (m_accum / m_dt);
            return { steps, alpha };
        }

        double StepSeconds() const { return m_dt; }
        void   SetMaxCatchupSteps(int n) { m_maxSteps = n; }

    private:
        double m_dt;
        double m_accum = 0.0;
        int    m_maxSteps = 8;
        bool   m_initialized = false;
        std::chrono::high_resolution_clock::time_point m_prev{};
    };
}
