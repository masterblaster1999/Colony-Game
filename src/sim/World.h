#pragma once
#include <cstdint>

namespace sim {

class World {
public:
    bool Init();
    void Shutdown();

    void Update(double dt);

private:
    uint64_t m_tick = 0;
};

} // namespace sim
