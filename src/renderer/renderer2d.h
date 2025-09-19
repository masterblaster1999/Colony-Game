#pragma once
#include <cstdint>
#include <memory>

// Minimal, backend-agnostic 2D renderer interface.
// Keep this header free of platform/D3D headers.
namespace gfx
{
    struct Color
    {
        float r, g, b, a;
        static constexpr Color Black() { return {0,0,0,1}; }
        static constexpr Color Clear() { return {0,0,0,0}; }
    };

    // Thin 2D API; expand as needed.
    class IRenderer2D
    {
    public:
        virtual ~IRenderer2D() = default;

        // Called when the framebuffer size changes.
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // Frame scope
        virtual void Begin() = 0;
        virtual void End() = 0;

        // Primitives (minimal to start)
        virtual void DrawFilledRect(float x, float y, float w, float h, Color c) = 0;
    };

    // Forward declaration of the D3D11 device wrapper so renderer2d.h stays generic.
    class D3D11Device;

    // Factory for the D3D11-backed renderer.
    // Implement in your D3D11 renderer source (not provided here).
    std::unique_ptr<IRenderer2D> CreateRenderer2D_D3D11(D3D11Device& device);
} // namespace gfx
