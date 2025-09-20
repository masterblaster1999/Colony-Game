#pragma once
#include "renderer2d.h"
#include <vector>
#include <wrl/client.h>
struct D3D11Device;

namespace gfx {
class Renderer2D_D3D11 final : public IRenderer2D {
public:
  explicit Renderer2D_D3D11(D3D11Device& dev);
  void Resize(uint32_t w, uint32_t h) override;
  void Begin() override;
  void End() override;
  void DrawFilledRect(float x,float y,float w,float h, Color c) override;
private:
  D3D11Device& m_dev;
  // pipeline bits & a CPU-side quad cache...
};
std::unique_ptr<IRenderer2D> CreateRenderer2D_D3D11(D3D11Device& device);
}
