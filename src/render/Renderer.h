#pragma once
#include <cstdint>
#include <windows.h>

namespace render {

class Renderer {
public:
    bool Init(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void Resize(uint32_t width, uint32_t height);

private:
    HWND m_hwnd = nullptr;
    uint32_t m_w = 0, m_h = 0;
    // Hook up DXGI/D3D later; flip-model swapchain recommended. :contentReference[oaicite:4]{index=4}
};

} // namespace render
