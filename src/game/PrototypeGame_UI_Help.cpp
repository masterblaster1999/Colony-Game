#include "game/PrototypeGame_Impl.h"

#if defined(COLONY_WITH_IMGUI)

namespace colony::game {

void PrototypeGame::Impl::drawHelpWindow()
{
    if (!showHelp)
        return;

    ImGui::Begin("Help", &showHelp, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Controls");
    ImGui::Separator();

    ImGui::BulletText("WASD: Pan camera");
    ImGui::BulletText("Mouse wheel: Zoom");
    ImGui::BulletText("Space + drag: Pan (mouse)");
    ImGui::BulletText("Left click/drag: Paint plan (current tool)");
    ImGui::BulletText("Right click/drag: Erase plans (except Inspect)");
    ImGui::BulletText("Shift + Left-drag: Rectangle mode (build tools). Inspect: selection rectangle for blueprints.");
    ImGui::BulletText("Shift + Right-drag: Rectangle erase (plans; except Inspect)");
    ImGui::BulletText("Z: Undo plan edits");
    ImGui::BulletText("Y: Redo plan edits");

    ImGui::Separator();
    ImGui::TextUnformatted("Tools");
    ImGui::Separator();

    ImGui::BulletText("1: Inspect (click a tile to view info)");
    ImGui::BulletText("2: Floor plan");
    ImGui::BulletText("3: Wall plan");
    ImGui::BulletText("4: Farm plan");
    ImGui::BulletText("5: Stockpile plan");
    ImGui::BulletText("6: Erase plans");
    ImGui::BulletText("7: Priority (changes priority on existing plans)");
    ImGui::BulletText("8: Demolish (marks built tiles for removal)");
    ImGui::BulletText("   (Trees are natural tiles; demolish them to gain wood)");
    ImGui::BulletText("9: Blueprint Paste (stamps the current blueprint as plans)");

    ImGui::Separator();
    ImGui::TextUnformatted("World controls");
    ImGui::Separator();

    ImGui::BulletText("Inspect: left-click a colonist to select it");
    ImGui::BulletText("Drafted colonist: right-click world to order Move / Build / Harvest");
    ImGui::BulletText("Colony panel: assign colonist Roles (affects auto-jobs + move/work speed)");

    ImGui::Separator();
    ImGui::TextUnformatted("UI");
    ImGui::Separator();

    ImGui::BulletText("F1: Toggle colony panel");
    ImGui::BulletText("F2: Toggle help");
    ImGui::BulletText("Minimap (Colony panel): click/drag to jump the camera");

    ImGui::End();
}

} // namespace colony::game

#endif // COLONY_WITH_IMGUI
