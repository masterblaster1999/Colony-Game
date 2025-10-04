#pragma once
#include <cstdint>
#include <chrono>
#include <windows.h>

#include "render/Renderer.h"
#include "sim/World.h"

namespace core {

class App {
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void TickFrame();
    void Shutdown();

private:
    using clock = std::chrono::high_resolution_clock;
    render::Renderer m_renderer;
    sim::World      m_world;

    clock::time_point m_prev{};
    double m_accumulator = 0.0;
    const double m_dt = 1.0 / 60.0;
};

} // namespace core
