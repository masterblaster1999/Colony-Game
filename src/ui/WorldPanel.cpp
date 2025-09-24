#ifdef IMGUI_VERSION
#include "imgui.h"
#include "../world/WorldSystem.hpp"
#include "../render/TerrainMesh.hpp"
#include "../render/TerrainRenderer.hpp"

namespace cg {

struct WorldPanelState {
    bool dirty = false;
};

inline bool WorldPanel(cg::WorldSystem& world,
                       cg::TerrainRenderer& renderer,
                       float xyScale = 1.0f,
                       float zScale  = 1.0f)
{
    static WorldPanelState S;

    auto& P = world.params();
    if (ImGui::Begin("World")) {
        S.dirty |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &P.seed);
        S.dirty |= ImGui::SliderFloat("Base freq", &P.base_freq, 0.0005f, 0.02f, "%.5f");
        S.dirty |= ImGui::SliderFloat("Warp amp (px)", &P.warp_amp_px, 0.0f, 80.0f);
        S.dirty |= ImGui::SliderFloat("Warp freq", &P.warp_freq, 0.0005f, 0.02f, "%.5f");
        S.dirty |= ImGui::SliderInt("Thermal iters", &P.thermal_iters, 0, 80);
        S.dirty |= ImGui::SliderFloat("Talus", &P.talus, 0.1f, 2.0f);
        S.dirty |= ImGui::SliderFloat("Sea level", &P.sea_level, 0.0f, 0.9f);

        if (ImGui::Button("Rebuild") || S.dirty) {
            world.rebuild();
            auto mesh = BuildTerrainMesh(world.data(), xyScale, zScale);
            renderer.upload(mesh);
            S.dirty = false;
        }
    }
    ImGui::End();
    return true;
}

} // namespace cg
#endif // IMGUI_VERSION
