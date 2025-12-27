#include "game/PrototypeGame_Impl.h"

namespace colony::game {

#if defined(COLONY_WITH_IMGUI)

void PrototypeGame::Impl::drawHelpWindow()
{
    if (!showHelp)
        return;

    ImGui::SetNextWindowSize({520, 320}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Help", &showHelp)) {
        ImGui::TextUnformatted("Prototype Controls");
        ImGui::Separator();

        ImGui::BulletText("Mouse wheel: zoom (over World)");
        ImGui::BulletText("Middle-drag OR Space + Left-drag: pan");
        ImGui::BulletText("Left-drag: paint current tool");
        ImGui::BulletText("Right-drag: erase plans");
        ImGui::BulletText("Shift + Left-drag: paint a rectangle");
        ImGui::BulletText("Shift + Right-drag: erase a rectangle");

        ImGui::Separator();
        ImGui::BulletText("1: Inspect");
        ImGui::BulletText("2: Floor");
        ImGui::BulletText("3: Wall");
        ImGui::BulletText("4: Farm");
        ImGui::BulletText("5: Stockpile");
        ImGui::BulletText("6: Erase");
        ImGui::BulletText("7: Priority (paint Brush Priority onto existing plans)");

        ImGui::Separator();
        ImGui::BulletText("F1: Toggle panels");
        ImGui::BulletText("F2: Toggle help");
        ImGui::BulletText("F3: Runtime hotkeys (popup)");
        ImGui::BulletText("R: Reset world");
        ImGui::BulletText("P: Pause simulation");
        ImGui::BulletText("F5: Reload input bindings");
        ImGui::BulletText("Ctrl+S: Save world");
        ImGui::BulletText("Ctrl+L: Load world");
        ImGui::BulletText("Ctrl+Z: Undo plan placement");
        ImGui::BulletText("Ctrl+Y (or Ctrl+Shift+Z): Redo plan placement");
        ImGui::BulletText("PgUp/PgDn: Brush priority up/down (build higher priority first)");
        ImGui::BulletText("Bindings Editor: Colony panel -> Input Bindings");
    }
    ImGui::End();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
