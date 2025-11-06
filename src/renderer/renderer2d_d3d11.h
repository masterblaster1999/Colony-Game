#pragma once

#include <memory>
#include <cstdint>
#include "renderer2d.h"

// Forward declare your D3D11 device wrapper to keep this header platform‑clean.
struct D3D11Device;

namespace gfx {

class Renderer2D_D3D11 final : public IRenderer2D {
public:
    explicit Renderer2D_D3D11(D3D11Device& dev);
    ~Renderer2D_D3D11() override; // Defined in the .cpp after Impl is complete (PIMPL)

    // Non-copyable / non-movable (renderer holds GPU state and a device ref)
    Renderer2D_D3D11(const Renderer2D_D3D11&) = delete;
    Renderer2D_D3D11& operator=(const Renderer2D_D3D11&) = delete;
    Renderer2D_D3D11(Renderer2D_D3D11&&) noexcept = delete;
    Renderer2D_D3D11& operator=(Renderer2D_D3D11&&) noexcept = delete;

    // IRenderer2D interface
    void Resize(uint32_t w, uint32_t h) override;
    void Begin() override;
    void DrawFilledRect(float x, float y, float w, float h, Color c) override;
    void End() override;

private:
    struct Impl;                      // pImpl (hidden implementation)
    std::unique_ptr<Impl> m_impl;     // opaque implementation handle
    D3D11Device&          m_dev;      // non-owning reference to the D3D11 device wrapper
};

// Factory function to construct the D3D11 2D renderer
[[nodiscard]] std::unique_ptr<IRenderer2D>
CreateRenderer2D_D3D11(D3D11Device& device);

} // namespace gfx
