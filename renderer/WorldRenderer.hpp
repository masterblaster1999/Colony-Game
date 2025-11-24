#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace renderer
{
    // Forward declarations: these are defined in your existing renderer code.
    class RendererDevice;     // wraps D3D device/context or command queues
    class RendererResources;  // manages shared textures, buffers, PSOs, etc.
    struct Camera;            // your world->view->proj camera struct

    // A single tile instance on the world grid.
    // You can extend this later with biome, height, etc.
    struct TileInstance
    {
        float x = 0.0f;     // world-space X
        float y = 0.0f;     // world-space Y
        std::uint32_t tileId = 0;   // index into your tile atlas/material table
        std::uint32_t variant = 0;  // optional variation within the tile type
    };

    // A generic “sprite” instance: colonists, items, decorations, etc.
    struct SpriteInstance
    {
        float x = 0.0f;
        float y = 0.0f;
        float width  = 1.0f;
        float height = 1.0f;

        std::uint32_t spriteId = 0; // index into sprite atlas/material table
        std::uint32_t color    = 0xFFFFFFFFu; // packed RGBA or ARGB, up to you
    };

    // Aggregated input the game passes each frame. This is intentionally simple:
    // your simulation/game code is responsible for filling these vectors.
    struct WorldRenderData
    {
        const std::vector<TileInstance>*   tiles   = nullptr;
        const std::vector<SpriteInstance>* sprites = nullptr;

        // You can extend this later with debug overlays, selection outlines, etc.
        // e.g. const std::vector<DebugLine>* debugLines = nullptr;
    };

    // WorldRenderer: a high-level façade that knows how to draw the *world*:
    // - terrain / tiles
    // - entities / sprites
    // - overlays (later)
    //
    // It holds references to your low-level RendererDevice/Resources objects, but
    // does not own them; it just uses them to issue draw calls.
    class WorldRenderer
    {
    public:
        WorldRenderer(RendererDevice& device, RendererResources& resources);
        ~WorldRenderer();

        // Initialize any GPU-side objects needed for world rendering
        // (tile vertex/index buffers, pipelines, etc.). Right now this returns true
        // unconditionally; you can extend it with actual error checks later.
        bool initialize();

        // Notify the renderer of a window/backbuffer resize, so it can update
        // viewport-dependent state if needed.
        void onResize(int width, int height);

        // Main entry point: draw the world for the current frame.
        //
        // - 'camera' is your existing camera (view/proj, etc.).
        // - 'data' contains the tiles and sprite instances the game wants drawn.
        void render(const Camera& camera, const WorldRenderData& data);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace renderer
