#include "game/PrototypeGame_Impl.h"

#include "input/InputBindingParse.h"

#if defined(COLONY_WITH_IMGUI)

namespace colony::game {

namespace {

[[nodiscard]] std::string chordToString(std::span<const std::uint16_t> codes)
{
    namespace bp = colony::input::bindings;
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        if (i != 0)
            out.push_back('+');
        out += bp::InputCodeToToken(static_cast<std::uint32_t>(codes[i]));
    }
    return out;
}

[[nodiscard]] std::string actionBindsToString(const colony::input::InputMapper& input, colony::input::Action action)
{
    std::string out;
    const std::size_t count = input.BindingCount(action);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i != 0)
            out += ", ";
        out += chordToString(input.BindingChord(action, i));
    }
    if (out.empty())
        out = "(unbound)";
    return out;
}

} // namespace

void PrototypeGame::Impl::drawHelpWindow()
{
    if (!showHelp)
        return;

    ImGui::Begin("Help", &showHelp, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Controls");
    ImGui::Separator();

    ImGui::BulletText("Paint plans: Left click/drag (current tool)");
    ImGui::BulletText("Erase plans: Right click/drag (except Inspect)");
    ImGui::BulletText("Rectangle mode: Shift + drag (build tools). Inspect: selection rectangle for blueprints.");

    ImGui::Separator();
    ImGui::TextUnformatted("Movement / camera (bindable)");
    ImGui::Separator();

    {
        const std::string f  = actionBindsToString(input, colony::input::Action::MoveForward);
        const std::string b  = actionBindsToString(input, colony::input::Action::MoveBackward);
        const std::string l  = actionBindsToString(input, colony::input::Action::MoveLeft);
        const std::string r  = actionBindsToString(input, colony::input::Action::MoveRight);
        const std::string up = actionBindsToString(input, colony::input::Action::MoveUp);
        const std::string dn = actionBindsToString(input, colony::input::Action::MoveDown);

        ImGui::BulletText("Move Forward: %s", f.c_str());
        ImGui::BulletText("Move Back:    %s", b.c_str());
        ImGui::BulletText("Move Left:    %s", l.c_str());
        ImGui::BulletText("Move Right:   %s", r.c_str());
        ImGui::BulletText("Move Up:      %s", up.c_str());
        ImGui::BulletText("Move Down:    %s", dn.c_str());

        const std::string boost = actionBindsToString(input, colony::input::Action::SpeedBoost);
        const std::string fast  = actionBindsToString(input, colony::input::Action::MoveForwardFast);
        ImGui::BulletText("Speed Boost:  %s", boost.c_str());
        ImGui::BulletText("Forward Fast: %s", fast.c_str());

        ImGui::Separator();

        const std::string orbit = actionBindsToString(input, colony::input::Action::CameraOrbit);
        const std::string pan   = actionBindsToString(input, colony::input::Action::CameraPan);
        const std::string zin   = actionBindsToString(input, colony::input::Action::CameraZoomIn);
        const std::string zout  = actionBindsToString(input, colony::input::Action::CameraZoomOut);

        ImGui::BulletText("Orbit camera: %s", orbit.c_str());
        ImGui::BulletText("Pan camera:   %s", pan.c_str());
        ImGui::BulletText("Zoom in:      %s", zin.c_str());
        ImGui::BulletText("Zoom out:     %s", zout.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Editing / system (bindable)");
    ImGui::Separator();

    {
        const std::string undo  = actionBindsToString(input, colony::input::Action::Undo);
        const std::string redo  = actionBindsToString(input, colony::input::Action::Redo);
        const std::string save  = actionBindsToString(input, colony::input::Action::SaveWorld);
        const std::string load  = actionBindsToString(input, colony::input::Action::LoadWorld);
        const std::string rel   = actionBindsToString(input, colony::input::Action::ReloadBindings);
        const std::string prUp  = actionBindsToString(input, colony::input::Action::PlanPriorityUp);
        const std::string prDn  = actionBindsToString(input, colony::input::Action::PlanPriorityDown);

        ImGui::BulletText("Undo plan edits: %s", undo.c_str());
        ImGui::BulletText("Redo plan edits: %s", redo.c_str());
        ImGui::BulletText("Save world:      %s", save.c_str());
        ImGui::BulletText("Load world:      %s", load.c_str());
        ImGui::BulletText("Reload bindings: %s", rel.c_str());
        ImGui::BulletText("Priority up:     %s", prUp.c_str());
        ImGui::BulletText("Priority down:   %s", prDn.c_str());
        ImGui::TextDisabled("(You can change these via Panels \u2192 Input Bindings \u2192 Bindings Editor.)");
    }

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

    ImGui::BulletText("Inspect: left-click a colonist to select it (primary)");
    ImGui::BulletText("Inspect: Ctrl+left-click toggles multi-select");
    ImGui::BulletText("Inspect: Alt+left-click selects the room under the cursor");
    ImGui::BulletText("Drafted colonist(s): right-click to order Move / Build / Harvest");
    ImGui::BulletText("  Move applies to all selected colonists");
    ImGui::BulletText("  Build/Harvest apply to the primary selection only");
    ImGui::BulletText("  Shift+right-click queues orders");
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
