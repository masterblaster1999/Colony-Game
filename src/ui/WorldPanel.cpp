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

        if (ImGui::CollapsingHeader("Hydrology / Lakes")) {
            S.dirty |= ImGui::Checkbox("Depression fill (lakes)", &P.enable_depression_fill);
            S.dirty |= ImGui::SliderFloat("Fill epsilon", &P.fill_epsilon, 0.0f, 0.10f, "%.3f");
            S.dirty |= ImGui::SliderFloat("Lake min depth", &P.lake_min_depth, 0.0f, 10.0f, "%.2f");
            S.dirty |= ImGui::SliderInt("Lake min area (cells)", &P.lake_min_area, 0, 400);
        }

        if (ImGui::CollapsingHeader("Moisture from Water")) {
            S.dirty |= ImGui::Checkbox("Enable moisture-from-water", &P.moisture_from_water);
            S.dirty |= ImGui::SliderFloat("Water moisture strength", &P.moisture_water_strength, 0.0f, 1.0f);
            S.dirty |= ImGui::SliderFloat("Water moisture radius", &P.moisture_water_radius, 4.0f, 256.0f, "%.0f");
            S.dirty |= ImGui::Checkbox("Include ocean as moisture source", &P.moisture_include_ocean);
        }

        if (ImGui::CollapsingHeader("Terrain Stamps")) {
            S.dirty |= ImGui::Checkbox("Enable stamps", &P.enable_stamps);

            S.dirty |= ImGui::SliderInt("Crater count", &P.crater_count, 0, 64);
            S.dirty |= ImGui::SliderFloat("Crater radius min", &P.crater_radius_min, 2.0f, 128.0f, "%.0f");
            S.dirty |= ImGui::SliderFloat("Crater radius max", &P.crater_radius_max, 2.0f, 256.0f, "%.0f");
            S.dirty |= ImGui::SliderFloat("Crater depth", &P.crater_depth, 0.0f, 40.0f, "%.1f");
            S.dirty |= ImGui::SliderFloat("Crater rim height", &P.crater_rim_height, 0.0f, 20.0f, "%.1f");

            S.dirty |= ImGui::SliderInt("Volcano count", &P.volcano_count, 0, 32);
            S.dirty |= ImGui::SliderFloat("Volcano radius min", &P.volcano_radius_min, 2.0f, 160.0f, "%.0f");
            S.dirty |= ImGui::SliderFloat("Volcano radius max", &P.volcano_radius_max, 2.0f, 320.0f, "%.0f");
            S.dirty |= ImGui::SliderFloat("Volcano height", &P.volcano_height, 0.0f, 60.0f, "%.1f");
            S.dirty |= ImGui::SliderFloat("Volcano crater ratio", &P.volcano_crater_ratio, 0.05f, 0.60f, "%.2f");

            S.dirty |= ImGui::SliderFloat("Stamp spacing", &P.stamp_min_spacing, 0.50f, 1.50f, "%.2f");
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
