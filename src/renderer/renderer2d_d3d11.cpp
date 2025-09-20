#include "renderer2d_d3d11.h"
#include "render/d3d11_device.h" // your wrapper
using Microsoft::WRL::ComPtr;

namespace gfx {
struct Vertex { float x,y; float r,g,b,a; };
Renderer2D_D3D11::Renderer2D_D3D11(D3D11Device& dev) : m_dev(dev) {
  // create VS/PS from precompiled blobs, input layout, VB/IB (dynamic), blend/sampler/raster states
}
void Renderer2D_D3D11::Resize(uint32_t w, uint32_t h) { /* update viewport ortho matrix if you add one */ }
void Renderer2D_D3D11::Begin() { /* map VB begin */ }
void Renderer2D_D3D11::DrawFilledRect(float x,float y,float w,float h, Color c) {
  // push 6 vertices (two tris) into a CPU vector
}
void Renderer2D_D3D11::End() {
  // map/update VB once, bind pipeline, draw all
}
std::unique_ptr<IRenderer2D> CreateRenderer2D_D3D11(D3D11Device& d) {
  return std::make_unique<Renderer2D_D3D11>(d);
}
} // namespace gfx
