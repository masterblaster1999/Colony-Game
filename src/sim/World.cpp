#include "World.h"

namespace sim {

bool World::Init() {
    m_tick = 0;
    return true;
}

void World::Shutdown() {}

void World::Update(double) {
    ++m_tick;
}

} // namespace sim
