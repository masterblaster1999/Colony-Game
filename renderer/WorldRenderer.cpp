#include "renderer/WorldRenderer.hpp"

// We only need <utility> etc. because we deliberately *don’t* call any methods
// on RendererDevice/RendererResources here. That keeps this file independent of
// your existing renderer headers. You can include your actual renderer headers
// here once you start filling in the TODOs.
#include <utility>   // std::move
#include <cassert>

namespace renderer
{
    // Internal implementation struct: hides details from the header and lets
    // us change internals without recompiling everything that includes the header.
    struct WorldRenderer::Impl
    {
        RendererDevice&    device;
        RendererResources& resources;

        int viewportWidth  = 0;
        int viewportHeight = 0;

        Impl(RendererDevice& dev, RendererResources& res)
            : device(dev)
            , resources(res)
        {
        }

        // Called from WorldRenderer::initialize().
        bool initialize()
        {
            // TODO: set up GPU resources needed specifically for world rendering:
            //
            // - tile vertex/index buffers
            // - instance buffers
            // - PSO / shaders for terrain & sprites
            // - sampler states and constant buffers
            //
            // You can call into 'device' and 'resources' here once you hook this
            // up to your existing renderer code.
            return true;
        }

        void onResize(int width, int height)
        {
            viewportWidth  = width;
            viewportHeight = height;

            // TODO: if you have viewport-dependent resources (e.g. offscreen
            // render targets for shadows, SSAO, etc.), resize/recreate them here.
        }

        void render(const Camera& camera, const WorldRenderData& data)
        {
            (void)camera; // avoid unused parameter warnings for now

            // Defensive sanity checks: in a complete implementation you might
            // want to assert these in debug builds.
            assert(viewportWidth  >= 0);
            assert(viewportHeight >= 0);

            // NOTE: This is where you'd actually:
            //
            //  1. Bind the appropriate PSO / shaders for terrain.
            //  2. Upload or bind tile instance data (data.tiles).
            //  3. Issue draw calls for tiles.
            //
            //  4. Bind sprites PSO / shaders.
            //  5. Upload/bind sprite instance data (data.sprites).
            //  6. Issue draw calls for sprites.
            //
            // Because I can’t see your RendererDevice/Resources APIs, I’m not
            // calling any actual methods here. Instead, this function is a
            // clearly defined “hook point” where you plug in your real draw code.

            if (data.tiles) {
                // Example pseudo-code (to be replaced with real rendering):
                //
                // device.beginTilePass(camera, viewportWidth, viewportHeight);
                // device.drawTiles(*data.tiles, resources.getTileMaterial());
                // device.endTilePass();
                //
                // For now, we just acknowledge that we *would* draw them here.
            }

            if (data.sprites) {
                // Similarly, placeholder location for sprite rendering:
                //
                // device.beginSpritePass(camera, viewportWidth, viewportHeight);
                // device.drawSprites(*data.sprites, resources.getSpriteMaterial());
                // device.endSpritePass();
            }
        }
    };

    // -------- WorldRenderer public API --------

    WorldRenderer::WorldRenderer(RendererDevice& device, RendererResources& resources)
        : m_impl(std::make_unique<Impl>(device, resources))
    {
    }

    WorldRenderer::~WorldRenderer() = default;

    bool WorldRenderer::initialize()
    {
        return m_impl->initialize();
    }

    void WorldRenderer::onResize(int width, int height)
    {
        m_impl->onResize(width, height);
    }

    void WorldRenderer::render(const Camera& camera, const WorldRenderData& data)
    {
        m_impl->render(camera, data);
    }

} // namespace renderer
