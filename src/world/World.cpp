#include "colony/world/World.h"

namespace colony {

void World::reset() {
    agents_.clear();
    sim_time_ = 0.0;
    sim_step_ = 0;
}

size_t World::spawn_agent(Vec2 pos, Vec2 vel) {
    Agent a;
    a.pos = pos;
    a.pos_prev = pos;
    a.vel = vel;
    agents_.push_back(a);
    return agents_.size() - 1;
}

void World::integrate_agents(double dt) {
    for (auto& a : agents_) {
        // Store previous for render interpolation
        a.pos_prev = a.pos;
        // Super-simple integration for demo purposes
        a.pos = a.pos + a.vel * dt;
    }
}

void World::tick(double dt_fixed) {
    integrate_agents(dt_fixed);
    sim_time_ += dt_fixed;
    ++sim_step_;
}

RenderSnapshot World::snapshot(float alpha) const {
    RenderSnapshot s;
    s.agent_positions.reserve(agents_.size());
    for (const auto& a : agents_) {
        s.agent_positions.emplace_back(lerp(a.pos_prev, a.pos, static_cast<double>(alpha)));
    }
    s.sim_time = sim_time_;
    s.sim_step = sim_step_;
    return s;
}

} // namespace colony
