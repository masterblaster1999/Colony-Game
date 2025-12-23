#ifdef IMGUI_VERSION
#include "imgui.h"
#include <cstdint>
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


ImGui::Separator();
ImGui::TextUnformatted("Settlement / Roads");
S.dirty |= ImGui::Checkbox("Enable settlement layer", &P.enable_settlement_layer);

if (P.enable_settlement_layer) {
    S.dirty |= ImGui::Checkbox("Build roads", &P.build_roads);

    S.dirty |= ImGui::SliderInt("Site sample step", &P.site_sample_step, 1, 16);
    S.dirty |= ImGui::SliderInt("Top site candidates", &P.top_site_candidates, 128, 8192);
    S.dirty |= ImGui::SliderFloat("Water preferred dist", &P.water_preferred_dist, 0.0f, 32.0f);
    S.dirty |= ImGui::SliderFloat("Water max dist", &P.water_max_dist, 16.0f, 256.0f);
    S.dirty |= ImGui::SliderFloat("Max slope for sites", &P.max_slope_for_sites, 0.05f, 1.0f);

    S.dirty |= ImGui::SliderInt("Settlements min", &P.settlements_min, 0, 16);
    S.dirty |= ImGui::SliderInt("Settlements max", &P.settlements_max, 0, 16);
    S.dirty |= ImGui::SliderFloat("Settlement min dist", &P.settlement_min_dist, 20.0f, 260.0f);
    S.dirty |= ImGui::SliderFloat("Settlement score cut", &P.settlement_score_cut, 0.0f, 1.0f);

    if (P.build_roads) {
        S.dirty |= ImGui::SliderFloat("Road slope cost", &P.road_slope_cost, 0.0f, 80.0f);
        S.dirty |= ImGui::SliderFloat("Road river penalty", &P.road_river_penalty, 0.0f, 120.0f);
        S.dirty |= ImGui::SliderFloat("Road biome penalty", &P.road_biome_penalty, 0.0f, 30.0f);
    }

    S.dirty |= ImGui::Checkbox("Stamp farmland", &P.stamp_farmland);
    if (P.stamp_farmland) {
        S.dirty |= ImGui::SliderFloat("Farmland radius", &P.farmland_radius, 0.0f, 120.0f);
        int ff = static_cast<int>(P.farmland_min_fertility);
        if (ImGui::SliderInt("Farmland min fertility", &ff, 0, 255)) {
            P.farmland_min_fertility = static_cast<std::uint8_t>(ff);
            S.dirty = true;
        }
    }

    S.dirty |= ImGui::Checkbox("Stamp forest", &P.stamp_forest);
    if (P.stamp_forest) {
        int mm = static_cast<int>(P.forest_min_moisture);
        if (ImGui::SliderInt("Forest min moisture", &mm, 0, 255)) {
            P.forest_min_moisture = static_cast<std::uint8_t>(mm);
            S.dirty = true;
        }
    }
}

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
