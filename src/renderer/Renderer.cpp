#include "Renderer.h"
#include <windows.h>

namespace render {

bool Renderer::Init(HWND hwnd, uint32_t width, uint32_t height) {
    m_hwnd = hwnd; m_w = width; m_h = height;
    // TODO: create D3D device + flip-model swap chain here.
    return true;
}

void Renderer::Shutdown() {
    // TODO: release D3D resources
}

void Renderer::BeginFrame() {
    // TODO: clear render target
}

void Renderer::EndFrame() {
    // TODO: Present (with vsync or tearing when supported)
}

void Renderer::Resize(uint32_t width, uint32_t height) {
    m_w = width; m_h = height;
    // TODO: resize swap chain buffers
}

} // namespace render
