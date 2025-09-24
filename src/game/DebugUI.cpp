#include "DebugUI.h"
#include "imgui.h"

#include <random>

using namespace cg::game;

static uint32_t RandomSeed()
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> dist(1, 0x7fffffff);
    return dist(rng);
}

void cg::game::DrawDebugUI(DebugSettings& s, const DebugCallbacks& cb,
                           float fps, float msPerFrame)
{
    if (!s.showHud) return;

    // ---- HUD ---------------------------------------------------
    ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration
                              | ImGuiWindowFlags_AlwaysAutoResize
                              | ImGuiWindowFlags_NoNav
                              | ImGuiWindowFlags_NoFocusOnAppearing
                              | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    if (ImGui::Begin("HUD", &s.showHud, hudFlags))
    {
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, msPerFrame);
        ImGui::SeparatorText("Sim");
        ImGui::SliderFloat("Speed (x)", &s.simSpeed, 0.25f, 4.0f, "%.2f");
        if (cb.setSimSpeed) cb.setSimSpeed(s.simSpeed);

        ImGui::SeparatorText("Presentation");
        ImGui::Checkbox("VSync", &s.vsync);
        // (Your Present code should read s.vsync each frame)
    }
    ImGui::End();

    // ---- Settings ------------------------------------------------
    if (ImGui::Begin("World Settings"))
    {
        ImGui::SeparatorText("World Seed");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &s.seed);
        ImGui::SameLine();
        if (ImGui::Button("Randomize")) s.seed = RandomSeed();

        ImGui::SeparatorText("Noise");
        ImGui::SliderFloat("Frequency",  &s.noise.frequency,  0.0001f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt  ("Octaves",    &s.noise.octaves,    1, 8);
        ImGui::SliderFloat("Lacunarity", &s.noise.lacunarity, 1.5f, 3.5f);
        ImGui::SliderFloat("Gain",       &s.noise.gain,       0.1f, 0.9f);
        ImGui::SliderFloat("Warp Amp",   &s.noise.warpAmp,    0.0f, 1.0f);
        ImGui::SliderFloat("Warp Freq",  &s.noise.warpFreq,   0.0001f, 0.2f, "%.4f", ImGuiSliderFlags_Logarithmic);

        ImGui::SeparatorText("Pathfinding");
        static const char* kAlgoNames[] = { "A*", "JPS", "HPA*", "Flow Field" };
        int algo = static_cast<int>(s.pathAlgo);
        if (ImGui::Combo("Algorithm", &algo, kAlgoNames, IM_ARRAYSIZE(kAlgoNames)))
        {
            s.pathAlgo = static_cast<PathAlgo>(algo);
        }

        if (ImGui::Button("Rebuild Navigation"))
        {
            s.requestRebuildNav = true;
            if (cb.rebuildNavigation) cb.rebuildNavigation(s.pathAlgo);
        }

        if (ImGui::Button("Regenerate Map (apply seed/noise)"))
        {
            s.requestRegen = true;
            if (cb.regenerateWorld) cb.regenerateWorld(s.seed, s.noise, s.pathAlgo);
        }
    }
    ImGui::End();
}
