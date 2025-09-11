#pragma once
#include <cstdint>
#include <memory>

struct HWND__; using HWND = HWND__*; // forward decl to avoid <windows.h>

namespace colony::gfx {

struct Color { float r,g,b,a; };
struct FrameSize { int w, h; };

class Renderer2D {
public:
  virtual ~Renderer2D() = default;
  virtual void begin_frame(FrameSize target) = 0;
  virtual void draw_quad(float x, float y, float w, float h, Color c) = 0;
  virtual void draw_line(float x0,float y0,float x1,float y1, Color c) = 0;
  virtual void draw_text(const wchar_t* txt, float x, float y, Color c) = 0;
  virtual void end_frame() = 0;
  virtual void present(bool vsync) = 0;
};

std::unique_ptr<Renderer2D> make_renderer_soft();     // uses mini_renderer.h
std::unique_ptr<Renderer2D> make_renderer_d3d11(HWND hwnd);

} // namespace colony::gfx
