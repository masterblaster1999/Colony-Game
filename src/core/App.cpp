#include "App.h"

namespace core {

bool App::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    if (!m_renderer.Init(hwnd, width, height)) return false;
    if (!m_world.Init()) return false;
    m_prev = clock::now();
    return true;
}

void App::TickFrame() {
    // Time step
    auto now = clock::now();
    double elapsed = std::chrono::duration<double>(now - m_prev).count();
    m_prev = now;

    // Clamp to avoid huge spikes
    if (elapsed > 0.25) elapsed = 0.25;
    m_accumulator += elapsed;

    // Fixed-step sim
    while (m_accumulator >= m_dt) {
        m_world.Update(m_dt);
        m_accumulator -= m_dt;
    }

    // Render
    m_renderer.BeginFrame();
    // TODO: draw world
    m_renderer.EndFrame();
}

void App::Shutdown() {
    m_world.Shutdown();
    m_renderer.Shutdown();
}

} // namespace core
