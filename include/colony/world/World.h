#pragma once
#include <cstdint>
#include <vector>
#include "colony/world/Math.h"

namespace colony {

// Minimal example data the renderer can interpolate.
// In your game you'll have many systems; this shows the pattern.
struct Agent {
    Vec2 pos;       // current (after last tick)
    Vec2 pos_prev;  // previous (before last tick)
    Vec2 vel;       // simple linear velocity for example
};

struct RenderSnapshot {
    std::vector<Vec2> agent_positions; // interpolated
    double  sim_time = 0.0;
    uint64_t sim_step = 0;
};

class World {
public:
    World() = default;

    void reset();
    void tick(double dt_fixed);

    // Returns interpolated positions for rendering (0..1).
    RenderSnapshot snapshot(float alpha) const;

    // Example API for testing; wire to your spawning logic.
    size_t spawn_agent(Vec2 pos, Vec2 vel);

    // Introspection
    double   sim_time() const { return sim_time_; }
    uint64_t sim_step() const { return sim_step_; }

private:
    void integrate_agents(double dt);

    std::vector<Agent> agents_;
    double   sim_time_ = 0.0;
    uint64_t sim_step_ = 0;
};

} // namespace colony
