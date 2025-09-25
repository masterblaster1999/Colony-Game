// src/core/FixedTimestep.h
#pragma once
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace core
{
    class FixedTimestep
    {
    public:
        struct Stats {
            double dt = 0.0;              // simulation step seconds (e.g., 1/60)
            double alpha = 0.0;           // interpolation factor [0,1)
            double frame_seconds = 0.0;   // wall time for this frame
            double fps = 0.0;             // instantaneous fps (optional)
            double sim_time = 0.0;        // total simulated seconds
            double real_time = 0.0;       // total real seconds observed
            int    steps_this_frame = 0;  // how many update() calls ran this frame
            std::uint64_t total_steps = 0;// cumulative update() calls
        };

        explicit FixedTimestep(double target_hz = 60.0,
                               double max_frame_seconds = 0.25 /* 250 ms cap */)
        : m_dt(1.0 / target_hz)
        , m_max_frame(std::max(0.001, max_frame_seconds))
        , m_prev(clock::now())
        {}

        // Change target Hz at runtime (keeps accumulator stable)
        void set_hz(double hz) {
            hz = std::clamp(hz, 1.0, 1000.0);
            m_dt = 1.0 / hz;
        }
        double hz() const { return 1.0 / m_dt; }
        double dt() const { return m_dt; }

        void set_time_scale(double s) { m_time_scale = std::clamp(s, 0.0, 8.0); }
        double time_scale() const { return m_time_scale; }

        void set_paused(bool p) { m_paused = p; }
        bool paused() const { return m_paused; }

        // Schedule a single update step even when paused (processed next tick)
        void step_once() { m_step_once = true; }

        template <class UpdateFn, class RenderFn>
        Stats tick(UpdateFn&& update, RenderFn&& render)
        {
            const auto now = clock::now();
            double frame = seconds(now - m_prev);
            m_prev = now;

            // Avoid the "spiral of death" on long stalls (minimize catch-up).
            frame = std::min(frame, m_max_frame);

            m_real_time += frame;
            Stats out{};
            out.dt = m_dt;
            out.frame_seconds = frame;
            out.fps = (frame > 0.0) ? (1.0 / frame) : 0.0;
            out.total_steps = m_total_steps;

            // Accumulate scaled time only if not paused
            if (!m_paused) {
                m_accumulator += frame * m_time_scale;
            }

            // Allow one step while paused (debug stepping)
            if (m_paused && m_step_once) {
                m_accumulator += m_dt;
                m_step_once = false;
            }

            // Hard safety: cap how many updates we’ll do in a single tick.
            // This avoids spending entire frames only catching up.
            const int max_steps_this_frame = m_max_steps_per_frame;

            int steps = 0;
            while (m_accumulator + 1e-12 >= m_dt && steps < max_steps_this_frame) {
                update(m_dt); // <-- your game/sim update
                m_accumulator -= m_dt;
                m_sim_time += m_dt;
                ++steps;
                ++m_total_steps;
            }

            out.steps_this_frame = steps;
            out.alpha = (m_dt > 0.0) ? (m_accumulator / m_dt) : 0.0;
            out.alpha = std::clamp(out.alpha, 0.0, 1.0);
            out.sim_time = m_sim_time;
            out.real_time = m_real_time;
            out.total_steps = m_total_steps;

            // Render *once* per frame with interpolation factor alpha
            render(out.alpha);

            return out;
        }

        // If your window is minimized / inactive and you want to freeze the sim
        void freeze_accumulator() { m_accumulator = 0.0; }

        // How many sim steps we allow each frame at most (default 120 = 2 seconds of 60 Hz)
        void set_max_steps_per_frame(int n) { m_max_steps_per_frame = std::max(1, n); }

    private:
        using clock = std::chrono::steady_clock;
        static inline double seconds(clock::duration d) {
            return std::chrono::duration<double>(d).count();
        }

        double m_dt = 1.0 / 60.0;          // simulation step
        double m_accumulator = 0.0;        // unconsumed time
        double m_max_frame = 0.25;         // clamp on big frames
        int    m_max_steps_per_frame = 120;// safety

        bool   m_paused = false;
        bool   m_step_once = false;
        double m_time_scale = 1.0;

        clock::time_point m_prev = clock::now();
        double m_sim_time = 0.0;
        double m_real_time = 0.0;
        std::uint64_t m_total_steps = 0;
    };
} // namespace core
