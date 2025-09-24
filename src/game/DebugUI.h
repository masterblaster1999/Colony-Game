#pragma once
#include <cstdint>
#include <functional>

namespace cg::game
{
    enum class PathAlgo : int { AStar = 0, JPS, HPAStar, FlowField };

    struct NoiseParams
    {
        float frequency   = 0.01f;   // base frequency
        int   octaves     = 4;
        float lacunarity  = 2.0f;
        float gain        = 0.5f;
        float warpAmp     = 0.0f;    // domain warp amplitude
        float warpFreq    = 0.05f;   // domain warp frequency
    };

    struct DebugSettings
    {
        bool  vsync            = false;
        bool  showHud          = true;
        bool  showPerf         = true;
        bool  requestRegen     = false;
        bool  requestRebuildNav= false;

        uint32_t seed          = 1;
        NoiseParams noise;

        PathAlgo pathAlgo      = PathAlgo::AStar;
        float    simSpeed      = 1.0f; // 0.5x..4x
    };

    // Callbacks you wire into your engine:
    struct DebugCallbacks
    {
        std::function<void(uint32_t /*seed*/, const NoiseParams&, PathAlgo)> regenerateWorld;
        std::function<void(PathAlgo)> rebuildNavigation;
        std::function<void(float)> setSimSpeed;
    };

    // Draw the HUD; mutate settings and dispatch callbacks as needed.
    void DrawDebugUI(DebugSettings& settings, const DebugCallbacks& cb,
                     float fps, float msPerFrame);
}
