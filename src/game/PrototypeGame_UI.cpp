#include "game/PrototypeGame_Impl.h"

#include "input/InputBindingParse.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace colony::game {

void PrototypeGame::Impl::DrawUI() noexcept
{
#if defined(COLONY_WITH_IMGUI)
    drawUI();
#else
    (void)this;
#endif
}

#if defined(COLONY_WITH_IMGUI)

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

// Minimal std::string wrapper for ImGui::InputText without pulling in imgui_stdlib.
// Uses the standard CallbackResize pattern.
static int InputTextStdStringCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
        return 0;
    }
    return 0;
}

static bool InputTextStdString(const char* label, std::string* str, ImGuiInputTextFlags flags = 0)
{
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, str->data(), str->capacity() + 1, flags, InputTextStdStringCallback, str);
}

[[nodiscard]] std::string chordToString(std::span<const std::uint16_t> codes)
{
    namespace bp = colony::input::bindings;
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i) {
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
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0)
            out += ", ";
        out += chordToString(input.BindingChord(action, i));
    }
    return out;
}

struct CanvasXform {
    ImVec2 p0{};
    ImVec2 p1{};
    ImVec2 center{};
    float  tilePx = 24.f;
};

[[nodiscard]] ImVec2 worldToScreen(const DebugCameraState& cam, const CanvasXform& cx, ImVec2 world) noexcept
{
    // cam.panX/panY represent the world position at the *canvas center*.
    const float sx = cx.center.x + (world.x - cam.panX) * cx.tilePx;
    const float sy = cx.center.y + (world.y - cam.panY) * cx.tilePx;
    return {sx, sy};
}

[[nodiscard]] ImVec2 screenToWorld(const DebugCameraState& cam, const CanvasXform& cx, ImVec2 screen) noexcept
{
    const float wx = cam.panX + (screen.x - cx.center.x) / cx.tilePx;
    const float wy = cam.panY + (screen.y - cx.center.y) / cx.tilePx;
    return {wx, wy};
}

[[nodiscard]] ImU32 tileFillColor(proto::TileType t) noexcept
{
    switch (t) {
    case proto::TileType::Empty: return IM_COL32(18, 18, 20, 255);
    case proto::TileType::Floor: return IM_COL32(70, 70, 80, 255);
    case proto::TileType::Wall: return IM_COL32(30, 30, 34, 255);
    case proto::TileType::Farm: return IM_COL32(40, 90, 40, 255);
    case proto::TileType::Stockpile: return IM_COL32(110, 80, 30, 255);
    }
    return IM_COL32(255, 0, 255, 255);
}

[[nodiscard]] ImU32 tilePlanColor(proto::TileType t) noexcept
{
    // Same palette, but semi-transparent.
    const ImU32 c = tileFillColor(t);
    return (c & 0x00FFFFFFu) | 0x88000000u;
}

} // namespace

void PrototypeGame::Impl::drawHelpWindow()
{
    if (!showHelp)
        return;

    ImGui::SetNextWindowSize({460, 280}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Help", &showHelp)) {
        ImGui::TextUnformatted("Prototype Controls");
        ImGui::Separator();

        ImGui::BulletText("Mouse wheel: zoom (over World)");
        ImGui::BulletText("Middle-drag OR Space + Left-drag: pan");
        ImGui::BulletText("Left-drag: paint current tool");
        ImGui::BulletText("Right-drag: erase plans");

        ImGui::Separator();
        ImGui::BulletText("1: Inspect");
        ImGui::BulletText("2: Floor");
        ImGui::BulletText("3: Wall");
        ImGui::BulletText("4: Farm");
        ImGui::BulletText("5: Stockpile");
        ImGui::BulletText("6: Erase");

        ImGui::Separator();
        ImGui::BulletText("F1: Toggle panels");
        ImGui::BulletText("F2: Toggle help");
        ImGui::BulletText("F3: Runtime hotkeys (popup)");
        ImGui::BulletText("R: Reset world");
        ImGui::BulletText("P: Pause simulation");
        ImGui::BulletText("F5: Reload input bindings");
        ImGui::BulletText("Bindings Editor: Colony panel -> Input Bindings");
    }
    ImGui::End();
}

void PrototypeGame::Impl::drawPanelsWindow()
{
    if (!showPanels)
        return;

    ImGui::SetNextWindowSize({360, 520}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Colony")) {
        auto& inv = world.inventory();

        ImGui::Text("Population: %d", static_cast<int>(world.colonists().size()));
        ImGui::Text("Wood: %d", inv.wood);
        ImGui::Text("Food: %.1f", inv.food);
        ImGui::Text("Built Farms: %d", world.builtCount(proto::TileType::Farm));

        ImGui::Separator();
        ImGui::Text("Plans Pending: %d", world.plannedCount());

        if (ImGui::Button("Clear Plans")) {
            world.clearAllPlans();
            setStatus("Plans cleared");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset World")) {
            resetWorld();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Build Tools");

        auto toolRadio = [&](Tool t, const char* label, proto::TileType tile) {
            const bool selected = (tool == t);
            if (ImGui::RadioButton(label, selected)) {
                tool = t;
            }

            if (tile != proto::TileType::Empty) {
                ImGui::SameLine();
                ImGui::TextDisabled("(wood %d, %.1fs)",
                                   proto::TileWoodCost(tile),
                                   proto::TileBuildTimeSeconds(tile));
            }
        };

        toolRadio(Tool::Inspect, "1  Inspect", proto::TileType::Empty);
        toolRadio(Tool::Floor, "2  Floor", proto::TileType::Floor);
        toolRadio(Tool::Wall, "3  Wall", proto::TileType::Wall);
        toolRadio(Tool::Farm, "4  Farm", proto::TileType::Farm);
        toolRadio(Tool::Stockpile, "5  Stockpile", proto::TileType::Stockpile);
        toolRadio(Tool::Erase, "6  Erase", proto::TileType::Empty);

        ImGui::Separator();
        ImGui::TextUnformatted("Simulation");
        ImGui::Checkbox("Paused (P)", &paused);
        ImGui::SliderFloat("Speed", &simSpeed, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);

        ImGui::Separator();
        ImGui::TextUnformatted("Input Bindings");
        ImGui::Checkbox("Hot Reload", &bindingHotReloadEnabled);
        ImGui::SliderFloat("Poll Interval (s)", &bindingsPollInterval, 0.1f, 5.0f, "%.1f");

        if (bindingsLoadedPath.empty()) {
            ImGui::TextUnformatted("Active: (defaults)");
        } else {
            ImGui::TextWrapped("Active: %s", bindingsLoadedPath.string().c_str());
        }

        if (ImGui::Button("Reload Now (F5)")) {
            (void)loadBindings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Bindings Editor...")) {
            showBindingsEditor = true;
            bindingsEditorInit = false;
        }

        if (!statusText.empty() && statusTtl > 0.f) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", statusText.c_str());
        }
    }
    ImGui::End();
}

namespace {

[[nodiscard]] bool writeTextFile(const fs::path& path, std::string_view text, std::string& outError)
{
    outError.clear();

    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            outError = "Failed to create directories: " + path.parent_path().string();
            return false;
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        outError = "Failed to open file for writing: " + path.string();
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        outError = "Write failed: " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool parseBindingsField(std::string_view field,
                                     bool& outClear,
                                     std::vector<std::vector<std::uint32_t>>& outChords,
                                     std::string& outWarning,
                                     std::string& outError)
{
    namespace bp = colony::input::bindings;

    outClear = false;
    outChords.clear();
    outWarning.clear();
    outError.clear();

    field = bp::Trim(field);
    if (field.empty()) {
        outClear = true;
        return true;
    }

    std::vector<std::uint32_t> chordCodes;
    bool hadInvalid = false;
    std::string firstInvalid;

    for (auto part : bp::Split(field, ',')) {
        part = bp::Trim(part);
        if (part.empty())
            continue;

        if (bp::ParseChordString(part, chordCodes)) {
            outChords.emplace_back(chordCodes.begin(), chordCodes.end());
        } else {
            hadInvalid = true;
            if (firstInvalid.empty())
                firstInvalid = std::string(part);
        }
    }

    if (outChords.empty()) {
        if (!firstInvalid.empty())
            outError = "No valid chords. Example invalid: \"" + firstInvalid + "\"";
        else
            outError = "No valid chords.";
        return false;
    }

    if (hadInvalid) {
        outWarning = "Some invalid chords were ignored";
        if (!firstInvalid.empty())
            outWarning += ". Example: \"" + firstInvalid + "\"";
    }

    return true;
}

[[nodiscard]] std::string chordCodesToString(std::span<const std::uint32_t> codes)
{
    namespace bp = colony::input::bindings;
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i != 0)
            out.push_back('+');
        out += bp::InputCodeToToken(codes[i]);
    }
    return out;
}

} // namespace

void PrototypeGame::Impl::drawBindingsEditorWindow()
{
    if (!showBindingsEditor)
        return;

    // One-time init when opened.
    if (!bindingsEditorInit) {
        if (!bindingsLoadedPath.empty()) {
            bindingsEditorTargetPath = bindingsLoadedPath;
        } else if (!bindingCandidates.empty()) {
            bindingsEditorTargetPath = bindingCandidates.front().first;
        } else {
            bindingsEditorTargetPath = fs::path("assets") / "config" / "input_bindings.json";
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }

        bindingsEditorMessage.clear();
        bindingsEditorMessageTtl = 0.f;
        bindingsEditorInit = true;
    }

    // Fade message.
    if (bindingsEditorMessageTtl > 0.f) {
        bindingsEditorMessageTtl = std::max(0.f, bindingsEditorMessageTtl - ImGui::GetIO().DeltaTime);
        if (bindingsEditorMessageTtl == 0.f)
            bindingsEditorMessage.clear();
    }

    ImGui::SetNextWindowSize({720, 560}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bindings Editor", &showBindingsEditor)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Edit bindings as comma-separated chords.");
    ImGui::TextDisabled("Examples:  W, Up    |   Shift+W   |   MouseLeft   |   Ctrl+WheelUp");
    ImGui::TextDisabled("Wheel tokens: WheelUp, WheelDown");
    ImGui::Separator();

    ImGui::TextWrapped("Target file: %s", bindingsEditorTargetPath.string().c_str());

    // Buttons
    if (ImGui::Button("Apply (runtime)"))
    {
        constexpr std::size_t kActCount = static_cast<std::size_t>(colony::input::Action::Count);
        std::array<bool, kActCount> clearFlags{};
        std::array<std::vector<std::vector<std::uint32_t>>, kActCount> parsed{};

        std::string warnings;
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            std::string warn, err;
            if (!parseBindingsField(bindingsEditorText[i], clearFlags[i], parsed[i], warn, err))
            {
                const auto a = static_cast<colony::input::Action>(i);
                bindingsEditorMessage = std::string("Error in ") + colony::input::InputMapper::ActionName(a) + ": " + err;
                bindingsEditorMessageTtl = 6.f;
                setStatus("Bindings: apply failed", 3.f);
                parsed = {};
                clearFlags = {};
                ImGui::End();
                return;
            }
            if (!warn.empty())
            {
                const auto a = static_cast<colony::input::Action>(i);
                warnings += std::string("[") + colony::input::InputMapper::ActionName(a) + "] " + warn + "\n";
            }
        }

        // Apply atomically.
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            const auto a = static_cast<colony::input::Action>(i);
            if (clearFlags[i]) {
                input.ClearBindings(a);
                continue;
            }

            input.ClearBindings(a);
            for (const auto& chord : parsed[i]) {
                input.AddBinding(a, std::span<const std::uint32_t>(chord.data(), chord.size()));
            }
        }

        if (!warnings.empty()) {
            bindingsEditorMessage = warnings;
            bindingsEditorMessageTtl = 6.f;
        } else {
            bindingsEditorMessage = "Applied.";
            bindingsEditorMessageTtl = 2.f;
        }
        setStatus("Bindings: applied (runtime)", 2.f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Save (write file)"))
    {
        constexpr std::size_t kActCount = static_cast<std::size_t>(colony::input::Action::Count);
        std::array<bool, kActCount> clearFlags{};
        std::array<std::vector<std::vector<std::uint32_t>>, kActCount> parsed{};

        std::string warnings;
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            std::string warn, err;
            if (!parseBindingsField(bindingsEditorText[i], clearFlags[i], parsed[i], warn, err))
            {
                const auto a = static_cast<colony::input::Action>(i);
                bindingsEditorMessage = std::string("Error in ") + colony::input::InputMapper::ActionName(a) + ": " + err;
                bindingsEditorMessageTtl = 6.f;
                setStatus("Bindings: save failed", 3.f);
                ImGui::End();
                return;
            }
            if (!warn.empty())
            {
                const auto a = static_cast<colony::input::Action>(i);
                warnings += std::string("[") + colony::input::InputMapper::ActionName(a) + "] " + warn + "\n";
            }
        }

        const std::string ext = colony::input::bindings::ToLowerCopy(bindingsEditorTargetPath.extension().string());

        std::string fileText;
        if (ext == ".ini")
        {
            fileText += "[Bindings]\n";
            for (std::size_t i = 0; i < kActCount; ++i)
            {
                const auto a = static_cast<colony::input::Action>(i);
                fileText += colony::input::InputMapper::ActionName(a);
                fileText += " =";
                if (!clearFlags[i])
                {
                    fileText += " ";
                    for (std::size_t b = 0; b < parsed[i].size(); ++b) {
                        if (b != 0)
                            fileText += ", ";
                        fileText += chordCodesToString(std::span<const std::uint32_t>(parsed[i][b].data(), parsed[i][b].size()));
                    }
                }
                fileText += "\n";
            }
        }
        else
        {
            using json = nlohmann::json;
            json j;
            j["version"] = 1;

            json binds = json::object();
            for (std::size_t i = 0; i < kActCount; ++i)
            {
                const auto a = static_cast<colony::input::Action>(i);
                json arr = json::array();

                if (!clearFlags[i])
                {
                    for (const auto& chord : parsed[i]) {
                        arr.push_back(chordCodesToString(std::span<const std::uint32_t>(chord.data(), chord.size())));
                    }
                }

                binds[colony::input::InputMapper::ActionName(a)] = arr;
            }

            j["bindings"] = std::move(binds);
            fileText = j.dump(2);
            fileText.push_back('\n');
        }

        std::string error;
        if (!writeTextFile(bindingsEditorTargetPath, fileText, error))
        {
            bindingsEditorMessage = error;
            bindingsEditorMessageTtl = 6.f;
            setStatus("Bindings: save failed", 3.f);
            ImGui::End();
            return;
        }

        // Reload bindings from disk so the running game matches the saved file,
        // and refresh hot-reload timestamps.
        (void)loadBindings();

        if (!warnings.empty()) {
            bindingsEditorMessage = std::string("Saved (with warnings):\n") + warnings;
            bindingsEditorMessageTtl = 6.f;
        } else {
            bindingsEditorMessage = "Saved.";
            bindingsEditorMessageTtl = 2.f;
        }
        setStatus("Bindings: saved", 2.f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }
        bindingsEditorMessage = "Reverted.";
        bindingsEditorMessageTtl = 1.5f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults"))
    {
        input.SetDefaultBinds();
        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }
        bindingsEditorMessage = "Defaults applied.";
        bindingsEditorMessageTtl = 2.f;
        setStatus("Bindings: defaults", 2.f);
    }

    if (!bindingsEditorMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", bindingsEditorMessage.c_str());
    }

    ImGui::Separator();

    if (ImGui::BeginTable("bindings_table", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 160.f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i)
        {
            const auto a = static_cast<colony::input::Action>(i);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(colony::input::InputMapper::ActionName(a));

            ImGui::TableNextColumn();
            const std::string label = std::string("##bind_") + colony::input::InputMapper::ActionName(a);
            InputTextStdString(label.c_str(), &bindingsEditorText[i]);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void PrototypeGame::Impl::drawWorldWindow()
{
    // First-run layout: attempt to occupy the available space.
    if (ImGuiViewport* vp = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!showPanels)
        flags |= ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("World", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // Canvas
    const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz       = ImGui::GetContentRegionAvail();
    if (canvas_sz.x < 50.f)
        canvas_sz.x = 50.f;
    if (canvas_sz.y < 50.f)
        canvas_sz.y = 50.f;
    const ImVec2 canvas_p1 = {canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y};

    ImGui::InvisibleButton("world_canvas", canvas_sz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    const bool canvas_hovered = ImGui::IsItemHovered();
    const bool canvas_active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(10, 10, 12, 255));
    dl->AddRect(canvas_p0, canvas_p1, IM_COL32(60, 60, 70, 255));

    const DebugCameraState& cam = camera.State();

    CanvasXform cx;
    cx.p0     = canvas_p0;
    cx.p1     = canvas_p1;
    cx.center = {canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f};
    cx.tilePx = 24.f * std::max(0.05f, cam.zoom);

    // Zoom around mouse cursor (only when hovering the canvas).
    //
    // NOTE: Zoom is driven through bindable input actions so the mouse wheel
    // can be re-mapped on Windows (WheelUp/WheelDown by default).
    if (canvas_hovered) {
        int detents = 0;
        for (const auto& ae : input.ActionEvents()) {
            if (ae.type != colony::input::ActionEventType::Pressed)
                continue;
            if (ae.action == colony::input::Action::CameraZoomIn)
                ++detents;
            else if (ae.action == colony::input::Action::CameraZoomOut)
                --detents;
        }

        if (detents != 0) {
            const float oldTilePx = cx.tilePx;
            const ImVec2 mouse    = ImGui::GetIO().MousePos;

            const ImVec2 worldBefore = screenToWorld(cam, cx, mouse);

            // Update zoom
            (void)camera.ApplyWheelDetents(detents);

            const DebugCameraState& cam2 = camera.State();
            cx.tilePx                    = 24.f * std::max(0.05f, cam2.zoom);

            // Adjust pan to keep the world point under the mouse stable.
            if (cx.tilePx != oldTilePx) {
                const float newPanX = worldBefore.x - (mouse.x - cx.center.x) / cx.tilePx;
                const float newPanY = worldBefore.y - (mouse.y - cx.center.y) / cx.tilePx;
                (void)camera.ApplyPan(newPanX - cam2.panX, newPanY - cam2.panY);
            }
        }
    }

    // Pan
    if (canvas_hovered && canvas_active) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;

        const bool middleDrag = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool spaceDrag  = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        if ((middleDrag || spaceDrag) && cx.tilePx > 0.f) {
            // "Grab" style: drag right -> world moves right
            const float wx = -d.x / cx.tilePx;
            const float wy = -d.y / cx.tilePx;
            (void)camera.ApplyPan(wx, wy);
        }
    }

    // Visible tile bounds
    const DebugCameraState& cam3 = camera.State();
    const ImVec2 worldMin        = screenToWorld(cam3, cx, canvas_p0);
    const ImVec2 worldMax        = screenToWorld(cam3, cx, canvas_p1);

    const int minX = std::max(0, static_cast<int>(std::floor(std::min(worldMin.x, worldMax.x))) - 1);
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(worldMin.y, worldMax.y))) - 1);
    const int maxX = std::min(world.width() - 1, static_cast<int>(std::ceil(std::max(worldMin.x, worldMax.x))) + 1);
    const int maxY = std::min(world.height() - 1, static_cast<int>(std::ceil(std::max(worldMin.y, worldMax.y))) + 1);

    // Draw tiles
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const proto::Cell& c = world.cell(x, y);

            const ImVec2 tileCenter = worldToScreen(cam3, cx, {x + 0.5f, y + 0.5f});
            const ImVec2 half       = {cx.tilePx * 0.5f, cx.tilePx * 0.5f};
            const ImVec2 p0         = {tileCenter.x - half.x, tileCenter.y - half.y};
            const ImVec2 p1         = {tileCenter.x + half.x, tileCenter.y + half.y};

            // Built layer
            dl->AddRectFilled(p0, p1, tileFillColor(c.built));

            // Planned overlay
            if (c.planned != proto::TileType::Empty && c.planned != c.built) {
                dl->AddRectFilled(p0, p1, tilePlanColor(c.planned));
                dl->AddRect(p0, p1, IM_COL32(220, 220, 255, 140));

                // Progress bar if reserved
                if (c.reservedBy >= 0 && c.workRemaining > 0.f) {
                    const float denom = std::max(0.01f, proto::TileBuildTimeSeconds(c.planned));
                    const float t = clampf(1.0f - (c.workRemaining / denom), 0.f, 1.f);
                    const ImVec2 bar0 = {p0.x + 2.f, p1.y - 6.f};
                    const ImVec2 bar1 = {p0.x + 2.f + (cx.tilePx - 4.f) * t, p1.y - 2.f};
                    dl->AddRectFilled(bar0, bar1, IM_COL32(255, 255, 255, 160));
                }
            }

            // Optional grid lines when zoomed in
            if (cx.tilePx >= 10.f) {
                dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 40));
            }
        }
    }

    // Draw colonists
    for (const proto::Colonist& c : world.colonists()) {
        const ImVec2 pos = worldToScreen(cam3, cx, {c.x, c.y});
        dl->AddCircleFilled(pos, std::max(2.f, cx.tilePx * 0.18f), IM_COL32(240, 240, 90, 255));

        if (c.hasJob) {
            const ImVec2 tgt = worldToScreen(cam3, cx, {c.targetX + 0.5f, c.targetY + 0.5f});
            dl->AddLine(pos, tgt, IM_COL32(240, 240, 90, 80), 1.0f);
        }
    }

    // Hover / interaction
    if (canvas_hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 w     = screenToWorld(cam3, cx, mouse);
        const int tx       = static_cast<int>(std::floor(w.x));
        const int ty       = static_cast<int>(std::floor(w.y));

        if (world.inBounds(tx, ty)) {
            // Hover highlight
            const ImVec2 tileCenter = worldToScreen(cam3, cx, {tx + 0.5f, ty + 0.5f});
            const ImVec2 half       = {cx.tilePx * 0.5f, cx.tilePx * 0.5f};
            const ImVec2 p0         = {tileCenter.x - half.x, tileCenter.y - half.y};
            const ImVec2 p1         = {tileCenter.x + half.x, tileCenter.y + half.y};
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120), 0.f, 0, 2.f);

            // Tooltip
            const proto::Cell& cell = world.cell(tx, ty);
            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", tx, ty);
            ImGui::Text("Built: %s", proto::TileTypeName(cell.built));
            if (cell.planned != proto::TileType::Empty && cell.planned != cell.built)
                ImGui::Text("Plan:  %s", proto::TileTypeName(cell.planned));
            ImGui::EndTooltip();

            // Painting
            const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space);

            if (!spaceHeld) {
                // Left paint: place current tool (except Inspect)
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && tool != Tool::Inspect) {
                    if (tx != lastPaintX || ty != lastPaintY) {
                        const auto result = world.placePlan(tx, ty, toolTile());
                        if (result == proto::PlacePlanResult::NotEnoughWood) {
                            setStatus("Not enough wood");
                        }
                        lastPaintX = tx;
                        lastPaintY = ty;
                    }
                }

                // Right paint: erase plan
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    if (tx != lastPaintX || ty != lastPaintY) {
                        (void)world.placePlan(tx, ty, proto::TileType::Empty);
                        lastPaintX = tx;
                        lastPaintY = ty;
                    }
                }

                // Inspect selection
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool == Tool::Inspect) {
                    setStatus(std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty));
                }
            }

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                lastPaintX = std::numeric_limits<int>::min();
                lastPaintY = std::numeric_limits<int>::min();
            }
        }
    }

    // Minimal HUD (top-left)
    {
        const auto& inv = world.inventory();
        char buf[256]   = {};
        (void)std::snprintf(buf,
                            sizeof(buf),
                            "Tool: %s | Wood: %d | Food: %.1f | Plans: %d",
                            toolName(),
                            inv.wood,
                            inv.food,
                            world.plannedCount());
        dl->AddText({canvas_p0.x + 8.f, canvas_p0.y + 8.f}, IM_COL32(255, 255, 255, 200), buf);
    }
    // Status overlay (also visible when panels are hidden).
    if (!statusText.empty() && statusTtl > 0.f) {
        float a = 1.0f;
        if (statusTtl < 0.5f)
            a = clampf(statusTtl / 0.5f, 0.f, 1.f);

        const ImU32 textCol = IM_COL32(255, 255, 255, static_cast<int>(200 * a));
        const ImU32 bgCol   = IM_COL32(0, 0, 0, static_cast<int>(140 * a));

        const ImVec2 pos = {canvas_p0.x + 8.f, canvas_p0.y + 28.f};
        const ImVec2 sz  = ImGui::CalcTextSize(statusText.c_str());

        dl->AddRectFilled({pos.x - 4.f, pos.y - 2.f},
                          {pos.x + sz.x + 4.f, pos.y + sz.y + 2.f},
                          bgCol,
                          4.0f);
        dl->AddText(pos, textCol, statusText.c_str());
    }

    ImGui::End();
}

void PrototypeGame::Impl::drawUI()
{
    drawWorldWindow();
    drawPanelsWindow();
    drawBindingsEditorWindow();
    drawHelpWindow();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
