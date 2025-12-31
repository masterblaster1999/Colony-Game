#include "game/PrototypeGame_Impl.h"

#include "util/PathUtf8.h"

#include "platform/win/PathUtilWin.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <shellapi.h>

namespace colony::game {

#if defined(COLONY_WITH_IMGUI)

namespace {

[[nodiscard]] ImU32 TileFillColor(proto::TileType t) noexcept
{
    switch (t) {
    case proto::TileType::Empty: return IM_COL32(18, 18, 20, 255);
    case proto::TileType::Floor: return IM_COL32(70, 70, 80, 255);
    case proto::TileType::Wall: return IM_COL32(30, 30, 34, 255);
    case proto::TileType::Farm: return IM_COL32(40, 90, 40, 255);
    case proto::TileType::Stockpile: return IM_COL32(110, 80, 30, 255);
    case proto::TileType::Door: return IM_COL32(145, 110, 55, 255);
    case proto::TileType::Tree: return IM_COL32(25, 115, 25, 255);
    case proto::TileType::Remove: return IM_COL32(160, 60, 60, 255);
    }
    return IM_COL32(255, 0, 255, 255);
}

[[nodiscard]] ImU32 TilePlanColor(proto::TileType t) noexcept
{
    const ImU32 c = TileFillColor(t);
    // Overwrite alpha with ~0x88.
    return (c & 0x00FFFFFFu) | 0x88000000u;
}

[[nodiscard]] proto::TileType SafeTileTypeFromNibble(std::uint8_t v) noexcept
{
    // TileType is currently 0..7 (up through Door); anything else is treated as Empty.
    if (v <= static_cast<std::uint8_t>(proto::TileType::Door))
        return static_cast<proto::TileType>(v);
    return proto::TileType::Empty;
}

[[nodiscard]] std::int64_t FileTimeToUnixSecondsUtc(fs::file_time_type ft) noexcept
{
    using namespace std::chrono;
    // Convert file_clock -> system_clock (best effort; stable enough for UI labels).
    const auto sctp = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(sctp.time_since_epoch()).count();
}

void DrawSaveThumbnail(const save::SaveSummary& s) noexcept
{
    const int tw = s.thumbW;
    const int th = s.thumbH;
    if (tw <= 0 || th <= 0)
        return;

    const std::size_t expected = static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);
    if (s.thumbPacked.size() != expected)
        return;

    // Fit inside a 128x128 square while preserving aspect ratio.
    float w = 128.0f;
    float h = 128.0f;
    if (tw > 0 && th > 0)
    {
        const float aspect = static_cast<float>(tw) / static_cast<float>(th);
        if (aspect >= 1.0f)
            h = w / aspect;
        else
            w = h * aspect;
    }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    // Reserve the space.
    ImGui::InvisibleButton("##save_thumb", ImVec2(w, h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(10, 10, 12, 255));
    dl->AddRect(p0, p1, IM_COL32(60, 60, 70, 255));

    const float cellW = w / static_cast<float>(tw);
    const float cellH = h / static_cast<float>(th);

    for (int y = 0; y < th; ++y)
    {
        for (int x = 0; x < tw; ++x)
        {
            const std::uint8_t packed = s.thumbPacked[static_cast<std::size_t>(y * tw + x)];
            const std::uint8_t builtN = packed & 0x0Fu;
            const std::uint8_t planN  = (packed >> 4) & 0x0Fu;

            const proto::TileType built = SafeTileTypeFromNibble(builtN);
            const proto::TileType plan  = SafeTileTypeFromNibble(planN);

            const ImVec2 a = ImVec2(p0.x + cellW * x,     p0.y + cellH * y);
            const ImVec2 b = ImVec2(p0.x + cellW * (x+1), p0.y + cellH * (y+1));

            dl->AddRectFilled(a, b, TileFillColor(built));

            // Planned overlay if it differs.
            if (plan != proto::TileType::Empty && plan != built)
                dl->AddRectFilled(a, b, TilePlanColor(plan));
        }
    }
}



void DrawBlueprintThumbnail(const colony::game::editor::PlanBlueprint& bp, bool includeEmpty) noexcept
{
    if (bp.Empty())
        return;

    const int bw = bp.w;
    const int bh = bp.h;
    if (bw <= 0 || bh <= 0)
        return;

    const std::size_t expected = static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh);
    if (bp.packed.size() != expected)
        return;

    // Fit inside a 128x128 square while preserving aspect ratio.
    float w = 128.0f;
    float h = 128.0f;
    if (bw > 0 && bh > 0)
    {
        const float aspect = static_cast<float>(bw) / static_cast<float>(bh);
        if (aspect >= 1.0f)
            h = w / aspect;
        else
            w = h * aspect;
    }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    // Reserve the space.
    ImGui::InvisibleButton("##blueprint_thumb", ImVec2(w, h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(10, 10, 12, 255));
    dl->AddRect(p0, p1, IM_COL32(60, 60, 70, 255));

    // Downsample large blueprints.
    const int sampleW = std::max(1, std::min(bw, 64));
    const int sampleH = std::max(1, std::min(bh, 64));

    const float cellW = w / static_cast<float>(sampleW);
    const float cellH = h / static_cast<float>(sampleH);

    for (int y = 0; y < sampleH; ++y)
    {
        const int wy = (y * bh) / sampleH;
        for (int x = 0; x < sampleW; ++x)
        {
            const int wx = (x * bw) / sampleW;
            const std::size_t idx = static_cast<std::size_t>(wy) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(wx);
            const std::uint8_t packed = bp.packed[idx];

            const proto::TileType plan = colony::game::editor::BlueprintUnpackTile(packed);
            if (plan == proto::TileType::Empty && !includeEmpty)
                continue;

            const ImVec2 a = ImVec2(p0.x + cellW * x,     p0.y + cellH * y);
            const ImVec2 b = ImVec2(p0.x + cellW * (x+1), p0.y + cellH * (y+1));

            if (plan == proto::TileType::Empty)
                dl->AddRectFilled(a, b, IM_COL32(220, 80, 80, 160));
            else
                dl->AddRectFilled(a, b, TileFillColor(plan));
        }
    }
}
} // namespace

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
        ImGui::Text("Trees: %d", world.builtCount(proto::TileType::Tree));
        ImGui::Text("Doors: %d", world.builtCount(proto::TileType::Door));
        ImGui::Text("Ready to Harvest: %d", world.harvestableFarmCount());

        // Hunger snapshot (v3+)
        {
            const float maxPersonalFood = static_cast<float>(std::max(0.0, world.colonistMaxPersonalFood));
            const float threshold = static_cast<float>(std::max(0.0, world.colonistEatThresholdFood));
            if (maxPersonalFood > 0.0f && !world.colonists().empty())
            {
                float sum = 0.0f;
                int hungry = 0;
                for (const auto& c : world.colonists())
                {
                    sum += std::max(0.0f, c.personalFood);
                    if (c.personalFood <= threshold)
                        ++hungry;
                }
                const float avg = sum / static_cast<float>(world.colonists().size());
                ImGui::Text("Avg Personal Food: %.1f / %.1f", avg, maxPersonalFood);
                ImGui::Text("Hungry: %d", hungry);
            }
        }

        if (ImGui::CollapsingHeader("Colonists"))
        {
            ImGui::TextDisabled("Inspect tool: left-click a colonist to select. Drafted colonists ignore auto build/harvest.\n"
                                "While drafted: right-click in the world to order Move / Build / Harvest.");

            if (ImGui::Button("Draft all"))
            {
                for (const auto& c : world.colonists())
                    world.SetColonistDrafted(c.id, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Undraft all"))
            {
                for (const auto& c : world.colonists())
                    world.SetColonistDrafted(c.id, false);
            }
            ImGui::SameLine();

            // Follow toggle only makes sense with a selection.
            if (selectedColonistId < 0)
                followSelectedColonist = false;

#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18400
            ImGui::BeginDisabled(selectedColonistId < 0);
            ImGui::Checkbox("Follow selected", &followSelectedColonist);
            ImGui::EndDisabled();
#else
            if (selectedColonistId < 0)
            {
                bool dummy = false;
                (void)ImGui::Checkbox("Follow selected", &dummy);
                ImGui::SameLine();
                ImGui::TextDisabled("(select a colonist)");
            }
            else
            {
                ImGui::Checkbox("Follow selected", &followSelectedColonist);
            }
#endif

            // Roles overview / quick assignment.
            {
                int buildCapable = 0;
                int farmCapable  = 0;
                int haulCapable  = 0;

                int buildEnabled = 0;
                int farmEnabled  = 0;
                int haulEnabled  = 0;

                for (const auto& c : world.colonists())
                {
                    const auto caps    = c.role.caps();
                    const bool canBuild = HasAny(caps, Capability::Building);
                    const bool canFarm  = HasAny(caps, Capability::Farming);
                    const bool canHaul  = HasAny(caps, Capability::Hauling);

                    if (canBuild) ++buildCapable;
                    if (canFarm)  ++farmCapable;
                    if (canHaul)  ++haulCapable;

                    if (canBuild && c.workPrio.build > 0) ++buildEnabled;
                    if (canFarm  && c.workPrio.farm  > 0) ++farmEnabled;
                    if (canHaul  && c.workPrio.haul  > 0) ++haulEnabled;
                }

                ImGui::TextDisabled("Role caps:   Build %d  Farm %d  Haul %d", buildCapable, farmCapable, haulCapable);
                ImGui::TextDisabled("Work enabled: Build %d  Farm %d  Haul %d", buildEnabled, farmEnabled, haulEnabled);

                if (ImGui::SmallButton("All Workers"))
                {
                    for (const auto& c : world.colonists())
                        (void)world.SetColonistRole(c.id, RoleId::Worker);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All Builders"))
                {
                    for (const auto& c : world.colonists())
                        (void)world.SetColonistRole(c.id, RoleId::Builder);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All Farmers"))
                {
                    for (const auto& c : world.colonists())
                        (void)world.SetColonistRole(c.id, RoleId::Farmer);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All Haulers"))
                {
                    for (const auto& c : world.colonists())
                        (void)world.SetColonistRole(c.id, RoleId::Hauler);
                }

                if (ImGui::SmallButton("Reset Work (role defaults)"))
                {
                    for (auto& c : world.colonists())
                        c.workPrio = proto::DefaultWorkPriorities(c.role.role);
                }

                const ImVec4 warnCol{1.0f, 0.75f, 0.25f, 1.0f};

                if (world.plannedCount() > 0)
                {
                    if (buildCapable == 0)
                        ImGui::TextColored(warnCol, "WARNING: No colonists can Build. Plans won't be completed.");
                    else if (buildEnabled == 0)
                        ImGui::TextColored(warnCol, "WARNING: Build is disabled by Work priorities (all Off).");
                }

                if (world.harvestableFarmCount() > 0)
                {
                    if (farmCapable == 0)
                        ImGui::TextColored(warnCol, "WARNING: No colonists can Farm. Harvests won't happen.");
                    else if (farmEnabled == 0)
                        ImGui::TextColored(warnCol, "WARNING: Farming is disabled by Work priorities (all Off).");
                }

                if (world.looseWoodTotal() > 0)
                {
                    if (haulCapable == 0)
                        ImGui::TextColored(warnCol, "WARNING: No colonists can Haul. Loose wood won't be collected.");
                    else if (haulEnabled == 0)
                        ImGui::TextColored(warnCol, "WARNING: Hauling is disabled by Work priorities (all Off).");
                }

            }

            const float maxPersonalFood = static_cast<float>(std::max(0.0, world.colonistMaxPersonalFood));

            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable
                                        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
            const float tableH = std::min(260.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f);
            if (ImGui::BeginTable("colonists_table", 11, flags, ImVec2(0, tableH)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Select");
                ImGui::TableSetupColumn("Draft");
                ImGui::TableSetupColumn("Role");
                ImGui::TableSetupColumn("Lvl");
                ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("F", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                ImGui::TableSetupColumn("Job");
                ImGui::TableSetupColumn("Food");
                ImGui::TableSetupColumn("Pos");
                ImGui::TableSetupColumn("Actions");
                ImGui::TableHeadersRow();

                for (auto& c : world.colonists())
                {
                    ImGui::TableNextRow();

                    // Select
                    ImGui::TableNextColumn();
                    char idLabel[16];
                    std::snprintf(idLabel, sizeof(idLabel), "C%02d", c.id);
                    const bool isSel = (c.id == selectedColonistId);
                    if (ImGui::Selectable(idLabel, isSel))
                    {
                        selectedColonistId = c.id;
                        selectedX = static_cast<int>(std::floor(c.x));
                        selectedY = static_cast<int>(std::floor(c.y));
                    }

                    // Draft
                    ImGui::TableNextColumn();
                    bool drafted = c.drafted;
                    char draftId[32];
                    std::snprintf(draftId, sizeof(draftId), "##draft_%d", c.id);
                    if (ImGui::Checkbox(draftId, &drafted))
                        world.SetColonistDrafted(c.id, drafted);

                    // Role
                    ImGui::TableNextColumn();
                    {
                        char roleId[32];
                        std::snprintf(roleId, sizeof(roleId), "##role_%d", c.id);
                        const char* preview = RoleDefOf(c.role.role).name;

                        if (ImGui::BeginCombo(roleId, preview))
                        {
                            for (int i = 0; i < static_cast<int>(RoleId::Count); ++i)
                            {
                                const RoleId rid = static_cast<RoleId>(i);
                                const bool selected = (c.role.role == rid);

                                if (ImGui::Selectable(RoleDefOf(rid).name, selected))
                                {
                                    (void)world.SetColonistRole(c.id, rid);
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();

                                if (ImGui::IsItemHovered())
                                {
                                    const auto caps = RoleDefOf(rid).caps;
                                    ImGui::BeginTooltip();
                                    ImGui::TextUnformatted(RoleDefOf(rid).name);
                                    ImGui::Separator();
                                    ImGui::Text("Move x%.2f  Work x%.2f", RoleDefOf(rid).moveMult, RoleDefOf(rid).workMult);
                                    ImGui::TextDisabled("Caps: %s%s%s%s",
                                        HasAny(caps, Capability::Building) ? "Build " : "",
                                        HasAny(caps, Capability::Farming)   ? "Farm "  : "",
                                        HasAny(caps, Capability::Hauling)   ? "Haul "  : "",
                                        HasAny(caps, Capability::Combat)    ? "Combat" : "");
                                    ImGui::EndTooltip();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }

                    // Level/XP
                    ImGui::TableNextColumn();
                    {
                        const auto lvl = static_cast<unsigned>(std::max<std::uint16_t>(1, c.role.level));
                        const auto xp  = static_cast<unsigned>(c.role.xp);
                        ImGui::Text("L%u", lvl);

                        if (ImGui::IsItemHovered())
                        {
                            const float moveEff = c.role.move() * (1.0f + 0.01f * static_cast<float>(lvl - 1));
                            const float workEff = c.role.work() * (1.0f + 0.02f * static_cast<float>(lvl - 1));
                            ImGui::BeginTooltip();
                            ImGui::Text("XP: %u/%u", xp, static_cast<unsigned>(RoleComponent::kXpPerLevel));
                            ImGui::Text("Effective move x%.2f", moveEff);
                            ImGui::Text("Effective work x%.2f", workEff);
                            ImGui::EndTooltip();
                        }

                        ImGui::SameLine();
                        ImGui::TextDisabled("%u/%u", xp, static_cast<unsigned>(RoleComponent::kXpPerLevel));
                    }

                    // Work priorities (Build / Farm / Haul)
                    static const char* kWorkPrioItems[] = {"Off", "1", "2", "3", "4"};

                    auto drawWorkPrio = [&](const char* id, std::uint8_t& prio, const char* tooltip)
                    {
                        int p = static_cast<int>(prio);
                        if (p < 0) p = 0;
                        if (p > 4) p = 4;

                        ImGui::SetNextItemWidth(34.0f);
                        if (ImGui::Combo(id, &p, kWorkPrioItems, IM_ARRAYSIZE(kWorkPrioItems)))
                            prio = static_cast<std::uint8_t>(p);

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(tooltip);
                            ImGui::EndTooltip();
                        }
                    };

                    // Build prio
                    ImGui::TableNextColumn();
                    {
                        char id[32];
                        std::snprintf(id, sizeof(id), "##prioB_%d", c.id);
                        drawWorkPrio(id, c.workPrio.build, "Build priority\n0=Off, 1=Highest, 4=Lowest");
                    }

                    // Farm prio
                    ImGui::TableNextColumn();
                    {
                        char id[32];
                        std::snprintf(id, sizeof(id), "##prioF_%d", c.id);
                        drawWorkPrio(id, c.workPrio.farm, "Farm priority\n0=Off, 1=Highest, 4=Lowest");
                    }

                    // Haul prio
                    ImGui::TableNextColumn();
                    {
                        char id[32];
                        std::snprintf(id, sizeof(id), "##prioH_%d", c.id);
                        drawWorkPrio(id, c.workPrio.haul, "Haul priority\n0=Off, 1=Highest, 4=Lowest");
                    }

                    // Job
                    ImGui::TableNextColumn();
                    const char* job = c.drafted && !c.hasJob ? "Drafted" : "Idle";
                    if (c.hasJob)
                    {
                        switch (c.jobKind)
                        {
                        case proto::Colonist::JobKind::Eat: job = "Eating"; break;
                        case proto::Colonist::JobKind::Harvest: job = "Harvest"; break;
                        case proto::Colonist::JobKind::BuildPlan: job = "Building"; break;
                    case proto::Colonist::JobKind::HaulWood: job = "Hauling"; break;
                        case proto::Colonist::JobKind::ManualMove: job = "Move"; break;
                        default: job = "Working"; break;
                        }
                    }
                    ImGui::TextUnformatted(job);

                if (!c.manualQueue.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Q%d", static_cast<int>(c.manualQueue.size()));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Manual order queue length");
                }

                    // Food
                    ImGui::TableNextColumn();
                    if (maxPersonalFood > 0.0f)
                        ImGui::Text("%.1f / %.1f", c.personalFood, maxPersonalFood);
                    else
                        ImGui::Text("%.1f", c.personalFood);

                    // Pos
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f, %.1f", c.x, c.y);

                    // Actions
                    ImGui::TableNextColumn();
                    char stopId[32];
                    std::snprintf(stopId, sizeof(stopId), "Stop##%d", c.id);
                    if (ImGui::SmallButton(stopId))
                        (void)world.CancelColonistJob(c.id);

                    ImGui::SameLine();

                    char clearQId[32];
                    std::snprintf(clearQId, sizeof(clearQId), "ClrQ##%d", c.id);
                    if (ImGui::SmallButton(clearQId))
                        c.manualQueue.clear();

                    ImGui::SameLine();
                    char resetId[32];
                    std::snprintf(resetId, sizeof(resetId), "XP0##%d", c.id);
                    if (ImGui::SmallButton(resetId))
                    {
                        c.role.level = 1;
                        c.role.xp = 0;
                    }

                    if (c.id == selectedColonistId)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Focus"))
                        {
                            const DebugCameraState& s = camera.State();
                            (void)camera.ApplyPan(c.x - s.panX, c.y - s.panY);
                        }
                    }
                }

                ImGui::EndTable();
            }
        }

// Selected colonist: manual order queue (drafted orders).
if (selectedColonistId >= 0)
{
    proto::Colonist* sel = nullptr;
    for (auto& c2 : world.colonists())
    {
        if (c2.id == selectedColonistId)
        {
            sel = &c2;
            break;
        }
    }

    if (sel && ImGui::TreeNode("Manual Orders"))
    {
        ImGui::Text("Queue length: %d", static_cast<int>(sel->manualQueue.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Queue"))
            sel->manualQueue.clear();

        ImGui::SameLine();
        if (ImGui::SmallButton("Pop Front") && !sel->manualQueue.empty())
            sel->manualQueue.erase(sel->manualQueue.begin());

        ImGui::TextDisabled("Tip: Shift+Right-click in the world (Inspect tool) to queue orders.");

        const bool frontActive =
            sel->hasJob && !sel->manualQueue.empty() &&
            ((sel->manualQueue.front().kind == proto::Colonist::ManualOrder::Kind::Move &&
              sel->jobKind == proto::Colonist::JobKind::ManualMove) ||
             (sel->manualQueue.front().kind == proto::Colonist::ManualOrder::Kind::Build &&
              sel->jobKind == proto::Colonist::JobKind::BuildPlan) ||
             (sel->manualQueue.front().kind == proto::Colonist::ManualOrder::Kind::Harvest &&
              sel->jobKind == proto::Colonist::JobKind::Harvest)) &&
            sel->targetX == sel->manualQueue.front().x && sel->targetY == sel->manualQueue.front().y;

        if (sel->manualQueue.empty())
        {
            ImGui::TextDisabled("(empty)");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(sel->manualQueue.size()); ++i)
            {
                auto& o = sel->manualQueue[i];

                const char* kind = "?";
                switch (o.kind)
                {
                case proto::Colonist::ManualOrder::Kind::Move:
                    kind = "Move";
                    break;
                case proto::Colonist::ManualOrder::Kind::Build:
                    kind = "Build";
                    break;
                case proto::Colonist::ManualOrder::Kind::Harvest:
                    kind = "Harvest";
                    break;
                default:
                    kind = "?";
                    break;
                }

                std::string label = std::to_string(i + 1) + ". " + kind + " @ " + std::to_string(o.x) + "," +
                                    std::to_string(o.y);
                if (i == 0 && frontActive)
                    label += " (active)";

                ImGui::TextUnformatted(label.c_str());

                ImGui::SameLine();
                const std::string upId = "Up##mq_up_" + std::to_string(i);
                if (ImGui::SmallButton(upId.c_str()) && i > 0)
                    std::swap(sel->manualQueue[i - 1], sel->manualQueue[i]);

                ImGui::SameLine();
                const std::string dnId = "Dn##mq_dn_" + std::to_string(i);
                if (ImGui::SmallButton(dnId.c_str()) && i + 1 < static_cast<int>(sel->manualQueue.size()))
                    std::swap(sel->manualQueue[i + 1], sel->manualQueue[i]);

                ImGui::SameLine();
                const std::string delId = "X##mq_del_" + std::to_string(i);
                if (ImGui::SmallButton(delId.c_str()))
                {
                    sel->manualQueue.erase(sel->manualQueue.begin() + i);
                    --i;
                }
            }
        }

        ImGui::TreePop();
    }
}

        ImGui::Separator();
        ImGui::Text("Plans Pending: %d", world.plannedCount());

        // Undo/redo
        {
            const bool canUndo = planHistory.CanUndo();
            const bool canRedo = planHistory.CanRedo();

            auto disabledButton = [&](const char* label, bool enabled) {
                // Use the public Dear ImGui API for disabling widgets.
                // (ImGuiItemFlags_Disabled lives in imgui_internal.h and isn't part of the stable API.)
                // BeginDisabled/EndDisabled was added in Dear ImGui 1.84.
                // If you're building against an older ImGui, we gracefully fall back to
                // a "visual-only" disable and gate the action below.
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18400
                if (!enabled)
                    ImGui::BeginDisabled(true);
                const bool pressed = ImGui::Button(label);
                if (!enabled)
                    ImGui::EndDisabled();
#else
                if (!enabled)
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                const bool pressed = ImGui::Button(label);
                if (!enabled)
                    ImGui::PopStyleVar();
#endif

                // Even if ImGui still reports a click for some edge-case (e.g. custom shortcuts),
                // we gate the action here.
                return pressed && enabled;
            };

            if (disabledButton("Undo (Ctrl+Z)", canUndo)) {
                (void)undoPlans();
            }
            ImGui::SameLine();
            if (disabledButton("Redo (Ctrl+Y)", canRedo)) {
                (void)redoPlans();
            }

            ImGui::TextDisabled("History: %zu undo / %zu redo", planHistory.UndoCount(), planHistory.RedoCount());
        }

        if (ImGui::Button("Clear Plans")) {
            // Make Clear Plans undoable (one command).
            planHistory.BeginCommand(inv.wood);

            int changed = 0;
            for (int y = 0; y < world.height(); ++y) {
                for (int x = 0; x < world.width(); ++x) {
                    const auto& beforeC = world.cell(x, y);
                    if (beforeC.planned == proto::TileType::Empty || beforeC.planned == beforeC.built)
                        continue;

                    colony::game::editor::PlanHistory::TileSnapshot before;
                    before.planned = beforeC.planned;
                    before.planPriority = beforeC.planPriority;
                    before.workRemaining = beforeC.workRemaining;

                    const auto r = world.placePlan(x, y, proto::TileType::Empty);
                    if (r != proto::PlacePlanResult::Ok)
                        continue;

                    const auto& afterC = world.cell(x, y);
                    colony::game::editor::PlanHistory::TileSnapshot after;
                    after.planned = afterC.planned;
                    after.planPriority = afterC.planPriority;
                    after.workRemaining = afterC.workRemaining;

                    planHistory.RecordChange(x, y, before, after);
                    ++changed;
                }
            }

            const bool committed = planHistory.CommitCommand(inv.wood);
            world.CancelAllJobsAndClearReservations();

            if (committed)
                setStatus("Plans cleared (" + std::to_string(changed) + ")", 2.5f);
            else
                setStatus("No plans to clear", 1.5f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset World")) {
            resetWorld();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Save / Load");

        // Slot 0 = "main" save file. Additional slots are separate files.
        if (ImGui::Button("Save World (Ctrl+S)")) { (void)saveWorld(); }
        ImGui::SameLine();
        if (ImGui::Button("Load World (Ctrl+L)")) { (void)loadWorld(); }

        ImGui::Spacing();
        ImGui::TextDisabled("Save slots");
        ImGui::InputInt("Slot", &saveSlot);
        saveSlot = std::clamp(saveSlot, 0, 9);

        const fs::path slotPath = worldSavePathForSlot(saveSlot);
        const std::string slotPathUtf8 = colony::util::PathToUtf8String(slotPath);

        if (ImGui::Button("Save Slot")) { (void)saveWorldToPath(slotPath, /*showStatus=*/true); }
        ImGui::SameLine();
        if (ImGui::Button("Load Slot")) { (void)loadWorldFromPath(slotPath, /*showStatus=*/true); }

        ImGui::TextWrapped("Slot path: %s", slotPathUtf8.c_str());
        if (ImGui::Button("Show Slot in Explorer"))
        {
            const std::wstring w = slotPath.wstring();
            std::wstring args = L"/select,\"" + w + L"\"";
            ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Autosave");
        ImGui::Checkbox("Enabled##autosave", &autosaveEnabled);
        ImGui::SliderFloat("Interval (sec)", &autosaveIntervalSeconds, 30.f, 3600.f, "%.0f");
        ImGui::SliderInt("Keep files", &autosaveKeepCount, 1, 20);

        const float nextIn = std::max(0.f, autosaveIntervalSeconds - autosaveAccumSeconds);
        if (autosaveEnabled && autosaveIntervalSeconds > 0.f)
            ImGui::Text("Next autosave in: %.0fs", nextIn);

        if (ImGui::Button("Autosave Now"))
        {
            autosaveAccumSeconds = 0.f;
            (void)autosaveWorld();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Autosave (Newest)"))
        {
            (void)loadWorldFromPath(autosavePathForIndex(0), /*showStatus=*/true);
        }

        const fs::path newestAuto = autosavePathForIndex(0);
        ImGui::TextWrapped("Newest autosave: %s", colony::util::PathToUtf8String(newestAuto).c_str());

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Save Browser"))
        {
            // Delete confirmation timeout (avoid permanent "armed delete").
            if (saveBrowserPendingDeleteTtl > 0.f)
            {
                saveBrowserPendingDeleteTtl = std::max(0.f, saveBrowserPendingDeleteTtl - ImGui::GetIO().DeltaTime);
                if (saveBrowserPendingDeleteTtl <= 0.f)
                    saveBrowserPendingDelete = -1;
            }

            auto refreshSaveBrowser = [&]() {
                saveBrowserEntries.clear();
                saveBrowserSelected = -1;
                saveBrowserPendingDelete = -1;
                saveBrowserPendingDeleteTtl = 0.f;

                const fs::path dir = worldSaveDir();

                // Slots 0..9
                for (int slot = 0; slot <= 9; ++slot)
                {
                    SaveBrowserEntry e;
                    e.kind = SaveBrowserEntry::Kind::Slot;
                    e.index = slot;
                    e.path = worldSavePathForSlot(slot);
                    e.metaPath = save::MetaPathFor(e.path);

                    std::error_code ec;
                    e.exists = fs::exists(e.path, ec) && !ec;
                    ec.clear();
                    e.metaExists = fs::exists(e.metaPath, ec) && !ec;

                    ec.clear();
                    if (e.exists)
                    {
                        e.sizeBytes = fs::file_size(e.path, ec);
                        if (ec) e.sizeBytes = 0;
                    }

                    if (e.metaExists)
                    {
                        std::string err;
                        e.metaOk = save::ReadMetaFile(e.metaPath, e.summary, &err);
                        if (!e.metaOk)
                            e.metaError = err;
                    }

                    
                    // Compute best-effort timestamp for display.
                    // Prefer meta's saved timestamp; fall back to filesystem mtime.
                    e.displayUnixSecondsUtc = 0;
                    e.timeFromMeta = false;
                    if (e.metaOk && e.summary.savedUnixSecondsUtc > 0)
                    {
                        e.displayUnixSecondsUtc = e.summary.savedUnixSecondsUtc;
                        e.timeFromMeta = true;
                    }
                    else
                    {
                        const fs::path tpath = e.exists ? e.path : (e.metaExists ? e.metaPath : fs::path{});
                        if (!tpath.empty())
                        {
                            std::error_code tec;
                            const fs::file_time_type ft = fs::last_write_time(tpath, tec);
                            if (!tec)
                                e.displayUnixSecondsUtc = FileTimeToUnixSecondsUtc(ft);
                        }
                    }

                    saveBrowserEntries.push_back(std::move(e));
                }

                // Autosaves 00..19 (we show only existing files)
                for (int i = 0; i < 20; ++i)
                {
                    char buf[32] = {};
                    std::snprintf(buf, sizeof(buf), "autosave_%02d.json", i);
                    const fs::path p = dir / buf;

                    SaveBrowserEntry e;
                    e.kind = SaveBrowserEntry::Kind::Autosave;
                    e.index = i;
                    e.path = p;
                    e.metaPath = save::MetaPathFor(e.path);

                    std::error_code ec;
                    e.exists = fs::exists(e.path, ec) && !ec;
                    ec.clear();
                    e.metaExists = fs::exists(e.metaPath, ec) && !ec;

                    if (!e.exists && !e.metaExists)
                        continue;

                    ec.clear();
                    if (e.exists)
                    {
                        e.sizeBytes = fs::file_size(e.path, ec);
                        if (ec) e.sizeBytes = 0;
                    }

                    if (e.metaExists)
                    {
                        std::string err;
                        e.metaOk = save::ReadMetaFile(e.metaPath, e.summary, &err);
                        if (!e.metaOk)
                            e.metaError = err;
                    }

                    
                    // Compute best-effort timestamp for display.
                    // Prefer meta's saved timestamp; fall back to filesystem mtime.
                    e.displayUnixSecondsUtc = 0;
                    e.timeFromMeta = false;
                    if (e.metaOk && e.summary.savedUnixSecondsUtc > 0)
                    {
                        e.displayUnixSecondsUtc = e.summary.savedUnixSecondsUtc;
                        e.timeFromMeta = true;
                    }
                    else
                    {
                        const fs::path tpath = e.exists ? e.path : (e.metaExists ? e.metaPath : fs::path{});
                        if (!tpath.empty())
                        {
                            std::error_code tec;
                            const fs::file_time_type ft = fs::last_write_time(tpath, tec);
                            if (!tec)
                                e.displayUnixSecondsUtc = FileTimeToUnixSecondsUtc(ft);
                        }
                    }

                    saveBrowserEntries.push_back(std::move(e));
                }

                // Select first existing entry by default.
                for (int i = 0; i < static_cast<int>(saveBrowserEntries.size()); ++i)
                {
                    if (saveBrowserEntries[i].exists)
                    {
                        saveBrowserSelected = i;
                        break;
                    }
                }

                saveBrowserDirty = false;
            };

            if (ImGui::Button("Refresh##savebrowser") || saveBrowserDirty)
                refreshSaveBrowser();

            ImGui::SameLine();
            if (ImGui::Button("Open Save Folder"))
            {
                const std::wstring wdir = worldSaveDir().wstring();
                ::ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }

            ImGui::TextWrapped("Folder: %s", colony::util::PathToUtf8String(worldSaveDir()).c_str());

            ImGui::BeginChild("##savebrowser_list", ImVec2(0, 140), true);
            for (int i = 0; i < static_cast<int>(saveBrowserEntries.size()); ++i)
            {
                const SaveBrowserEntry& e = saveBrowserEntries[i];

                std::string label;
                if (e.kind == SaveBrowserEntry::Kind::Slot)
                {
                    label = "Slot " + std::to_string(e.index);
                    if (e.index == 0)
                        label += " (default)";
                }
                else
                {
                    char tmp[32] = {};
                    std::snprintf(tmp, sizeof(tmp), "Autosave %02d", e.index);
                    label = tmp;
                }

                const std::string when = save::FormatLocalTime(e.displayUnixSecondsUtc);
                if (!when.empty())
                {
                    if (e.timeFromMeta)
                        label += "  [" + when + "]";
                    else
                        label += "  [" + when + " (mtime)]";
                }

                if (!e.exists)
                    label += "  (missing)";

                if (ImGui::Selectable(label.c_str(), saveBrowserSelected == i))
                    saveBrowserSelected = i;
            }
            ImGui::EndChild();

            if (saveBrowserSelected >= 0 && saveBrowserSelected < static_cast<int>(saveBrowserEntries.size()))
            {
                SaveBrowserEntry& e = saveBrowserEntries[saveBrowserSelected];

                ImGui::Spacing();
                ImGui::TextWrapped("Path: %s", colony::util::PathToUtf8String(e.path).c_str());
                if (e.exists)
                    ImGui::Text("File size: %.1f KB", static_cast<double>(e.sizeBytes) / 1024.0);

                if (e.metaExists)
                {
                    if (e.metaOk)
                    {
                        ImGui::Text("Saved: %s", save::FormatLocalTime(e.summary.savedUnixSecondsUtc).c_str());
                        ImGui::Text("Playtime: %s", save::FormatDurationHMS(e.summary.playtimeSeconds).c_str());
                        ImGui::Text("World: %dx%d", e.summary.worldW, e.summary.worldH);
                        ImGui::Text("Population: %d", e.summary.population);
                        ImGui::Text("Plans pending: %d", e.summary.plannedCount);
                        ImGui::Text("Wood: %d   Food: %.1f", e.summary.wood, e.summary.food);
                        ImGui::Text("Built: Floor %d | Wall %d | Farm %d | Stockpile %d",
                                    e.summary.builtFloors, e.summary.builtWalls, e.summary.builtFarms, e.summary.builtStockpiles);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Preview");
                        if (!e.summary.thumbPacked.empty() && e.summary.thumbW > 0 && e.summary.thumbH > 0)
                        {
                            DrawSaveThumbnail(e.summary);
                        }
                        else
                        {
                            ImGui::TextDisabled("(no thumbnail yet — make a new save to generate one)");
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Meta read failed: %s", e.metaError.c_str());
                    }
                }
                else
                {
                    ImGui::TextDisabled("No meta file (create a new save to generate one).");
                }

                if (!e.timeFromMeta && e.displayUnixSecondsUtc > 0)
                {
                    ImGui::Text("Modified: %s", save::FormatLocalTime(e.displayUnixSecondsUtc).c_str());
                }

                ImGui::Spacing();
                const bool canLoad = e.exists;
                if (ImGui::Button("Load Selected"))
                {
                    if (canLoad)
                        (void)loadWorldFromPath(e.path, /*showStatus=*/true);
                    else
                        setStatus("Save file missing", 2.0f);
                }

                ImGui::SameLine();
                if (ImGui::Button("Show in Explorer##savebrowser_show"))
                {
                    const std::wstring w = e.path.wstring();
                    std::wstring args = L"/select,\"" + w + L"\"";
                    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                }

                ImGui::SameLine();
                const bool canDelete = e.exists || e.metaExists;
                if (ImGui::Button("Delete##savebrowser_delete"))
                {
                    if (canDelete)
                    {
                        saveBrowserPendingDelete = saveBrowserSelected;
                        saveBrowserPendingDeleteTtl = 6.f;
                    }
                    else
                    {
                        setStatus("Nothing to delete", 2.0f);
                    }
                }

                if (saveBrowserPendingDelete == saveBrowserSelected)
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Confirm delete? (this cannot be undone)");

                    if (ImGui::Button("CONFIRM DELETE"))
                    {
                        const bool needWorld = e.exists;
                        const bool needMeta  = e.metaExists;

                        std::error_code ec1;
                        const bool ok1 = !needWorld || winpath::remove_with_retry(e.path, &ec1);

                        std::error_code ec2;
                        const bool ok2 = !needMeta || winpath::remove_with_retry(e.metaPath, &ec2);

                        if (ok1 && ok2)
                        {
                            setStatus("Deleted save entry", 2.0f);
                        }
                        else
                        {
                            const std::error_code& use = (!ok1 ? ec1 : ec2);
                            setStatus(std::string("Delete failed: ") + use.message(), 4.0f);
                        }

                        saveBrowserPendingDelete = -1;
                        saveBrowserPendingDeleteTtl = 0.f;
                        saveBrowserDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##savebrowser_cancel"))
                    {
                        saveBrowserPendingDelete = -1;
                        saveBrowserPendingDeleteTtl = 0.f;
                    }
                }
            }
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
        toolRadio(Tool::Priority, "7  Priority", proto::TileType::Empty);
        toolRadio(Tool::Demolish, "8  Demolish", proto::TileType::Remove);
        toolRadio(Tool::Blueprint, "9  Blueprint Paste", proto::TileType::Empty);

        if (tool == Tool::Priority)
            ImGui::TextDisabled("Paints the current Brush Priority onto existing plans (no cost).");

        if (tool == Tool::Demolish)
            ImGui::TextDisabled("Marks built tiles for deconstruction (refunds wood for player-built tiles). Use right-drag to clear plans.");

        if (tool == Tool::Blueprint)
            ImGui::TextDisabled("Stamps the current blueprint as plans (copy/load from the Blueprints section below). Right-drag still clears plans.");

        // Brush priority (0..3 -> P1..P4)
        {
            int brushP = std::clamp(planBrushPriority + 1, 1, 4);
            if (ImGui::SliderInt("Brush Priority", &brushP, 1, 4, "P%d"))
                planBrushPriority = std::clamp(brushP - 1, 0, 3);
            ImGui::TextDisabled("Higher priority plans are assigned first (default hotkeys: PgUp/PgDn).");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Selection");
        if (selectedX >= 0 && selectedY >= 0 && world.inBounds(selectedX, selectedY))
        {
            const proto::Cell& c = world.cell(selectedX, selectedY);
            ImGui::Text("Tile: (%d, %d)", selectedX, selectedY);
            ImGui::Text("Built: %s", proto::TileTypeName(c.built));
            ImGui::TextDisabled("%s", c.builtFromPlan ? "Player-built" : "Seeded");

            const int rid = world.roomIdAt(selectedX, selectedY);
            if (const proto::World::RoomInfo* ri = world.roomInfoById(rid))
            {
                ImGui::Text("Room: %s", ri->indoors ? "Indoors" : "Outdoors");
                ImGui::SameLine();
                ImGui::TextDisabled("(R%d, %d tiles)", ri->id, ri->area);
            }
            else
            {
                ImGui::TextDisabled("Room: (none)");
            }

            if (c.built == proto::TileType::Tree)
                ImGui::TextDisabled("Chop yield: %d wood", std::max(0, world.treeChopYieldWood));

            if (c.planned != proto::TileType::Empty && c.planned != c.built)
            {
                ImGui::Text("Plan:  %s", proto::TileTypeName(c.planned));
                ImGui::Text("Reserved by: %d", c.reservedBy);

                // Edit plan priority without changing the plan type.
                int pri = std::clamp(static_cast<int>(c.planPriority), 0, 3);
                const char* const items = "P1 (Low)\0P2\0P3\0P4 (High)\0";
                if (ImGui::Combo("Plan Priority", &pri, items))
                {
                    // Separate this from any in-progress paint stroke.
                    if (planHistory.HasActiveCommand())
                        (void)planHistory.CommitCommand(world.inventory().wood);

                    planHistory.BeginCommand(world.inventory().wood);

                    using PlanSnapshot = colony::game::editor::PlanHistory::TileSnapshot;
                    const PlanSnapshot before{c.planned, static_cast<std::uint8_t>(c.planPriority), c.workRemaining};

                    const auto r = world.placePlan(selectedX, selectedY, c.planned, static_cast<std::uint8_t>(pri));
                    if (r == proto::PlacePlanResult::Ok)
                    {
                        const proto::Cell& afterC = world.cell(selectedX, selectedY);
                        const PlanSnapshot after{afterC.planned, static_cast<std::uint8_t>(afterC.planPriority), afterC.workRemaining};
                        planHistory.RecordChange(selectedX, selectedY, before, after);

                        const bool committed = planHistory.CommitCommand(world.inventory().wood);
                        if (committed)
                            world.CancelAllJobsAndClearReservations();
                    }
                    else
                    {
                        planHistory.CancelCommand();
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No active plan on this tile.");
            }

            if (ImGui::Button("Clear selection"))
            {
                selectedX = -1;
                selectedY = -1;
            }
        }
        else
        {
            ImGui::TextDisabled("No selection (use Inspect tool and click a tile).");
        }



        ImGui::Separator();
        if (ImGui::CollapsingHeader("Blueprints", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Inspect: Shift + Left-drag in the world to select a rectangle.");

            const bool hasSel = (selectRectHas || selectRectActive);
            int sx0 = 0;
            int sy0 = 0;
            int sx1 = 0;
            int sy1 = 0;

            if (hasSel)
            {
                sx0 = std::clamp(std::min(selectRectStartX, selectRectEndX), 0, world.width() - 1);
                sy0 = std::clamp(std::min(selectRectStartY, selectRectEndY), 0, world.height() - 1);
                sx1 = std::clamp(std::max(selectRectStartX, selectRectEndX), 0, world.width() - 1);
                sy1 = std::clamp(std::max(selectRectStartY, selectRectEndY), 0, world.height() - 1);

                ImGui::Text("Selection: (%d,%d) -> (%d,%d)  (%dx%d)", sx0, sy0, sx1, sy1, (sx1 - sx0 + 1), (sy1 - sy0 + 1));
            }
            else
            {
                ImGui::TextDisabled("Selection: none");
            }

            ImGui::Checkbox("Copy plans only (ignore built tiles)", &blueprintCopyPlansOnly);

            if (ImGui::Button("Copy selection -> blueprint"))
            {
                if (!hasSel)
                {
                    setStatus("Blueprint copy: no selection (Inspect + Shift + drag).", 3.0f);
                }
                else
                {
                    const int bw = sx1 - sx0 + 1;
                    const int bh = sy1 - sy0 + 1;

                    blueprint.w = bw;
                    blueprint.h = bh;
                    blueprint.packed.assign(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), 0);

                    std::size_t nonEmpty = 0;

                    for (int y = 0; y < bh; ++y)
                    {
                        for (int x = 0; x < bw; ++x)
                        {
                            const proto::Cell& c = world.cell(sx0 + x, sy0 + y);
                            const bool hasActivePlan = (c.planned != proto::TileType::Empty && c.planned != c.built);

                            proto::TileType t = proto::TileType::Empty;
                            std::uint8_t pr = 0;

                            if (blueprintCopyPlansOnly)
                            {
                                if (hasActivePlan)
                                {
                                    t  = c.planned;
                                    pr = static_cast<std::uint8_t>(std::clamp(c.planPriority, 0, 3));
                                }
                            }
                            else
                            {
                                if (hasActivePlan)
                                {
                                    t  = c.planned;
                                    pr = static_cast<std::uint8_t>(std::clamp(c.planPriority, 0, 3));
                                }
                                else
                                {
                                    t  = c.built;
                                    pr = 0;
                                }
                            }

                            // Blueprints are for plans; clamp out non-plan tiles (e.g. Trees).
                            if (static_cast<std::uint8_t>(t) > static_cast<std::uint8_t>(proto::TileType::Remove))
                                t = proto::TileType::Empty;

                            if (t != proto::TileType::Empty)
                                ++nonEmpty;

                            blueprint.packed[static_cast<std::size_t>(y * bw + x)] = colony::game::editor::BlueprintPack(t, pr);
                        }
                    }

                    setStatus("Blueprint copied: " + std::to_string(bw) + "x" + std::to_string(bh) + " (" + std::to_string(nonEmpty) + " non-empty)");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear selection"))
            {
                selectRectActive = false;
                selectRectHas    = false;
                setStatus("Selection cleared");
            }

            ImGui::Separator();

            if (blueprint.Empty())
            {
                ImGui::TextDisabled("Blueprint: empty");
            }
            else
            {
                ImGui::Text("Blueprint: %dx%d", blueprint.w, blueprint.h);
                DrawBlueprintThumbnail(blueprint, blueprintPasteIncludeEmpty);
            }

            if (ImGui::Button("Copy blueprint -> clipboard"))
            {
                if (blueprint.Empty())
                {
                    setStatus("Blueprint copy: nothing to copy.", 3.0f);
                }
                else
                {
                    const std::string json = colony::game::editor::PlanBlueprintToJson(blueprint);
                    ImGui::SetClipboardText(json.c_str());
                    setStatus("Blueprint copied to clipboard");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load blueprint <- clipboard"))
            {
                const char* clip = ImGui::GetClipboardText();
                if (!clip || !clip[0])
                {
                    setStatus("Blueprint paste: clipboard is empty.", 3.0f);
                }
                else
                {
                    std::string err;
                    colony::game::editor::PlanBlueprint tmp;
                    if (!colony::game::editor::PlanBlueprintFromJson(clip, tmp, &err))
                    {
                        setStatus("Blueprint paste: invalid data. " + err, 4.0f);
                    }
                    else
                    {
                        blueprint = std::move(tmp);
                        setStatus("Blueprint loaded: " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear blueprint"))
            {
                blueprint.Clear();
                setStatus("Blueprint cleared");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Transforms");

            ImGui::BeginDisabled(blueprint.Empty());
            if (ImGui::Button("Rotate CW"))
            {
                blueprint = colony::game::editor::BlueprintRotateCW(blueprint);
                setStatus("Blueprint rotated: " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
            }
            ImGui::SameLine();
            if (ImGui::Button("Rotate CCW"))
            {
                blueprint = colony::game::editor::BlueprintRotateCCW(blueprint);
                setStatus("Blueprint rotated: " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
            }
            ImGui::SameLine();
            if (ImGui::Button("Rotate 180"))
            {
                blueprint = colony::game::editor::BlueprintRotate180(blueprint);
                setStatus("Blueprint rotated: " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
            }

            if (ImGui::Button("Flip Horizontal"))
            {
                blueprint = colony::game::editor::BlueprintFlipHorizontal(blueprint);
                setStatus("Blueprint flipped (horizontal)");
            }
            ImGui::SameLine();
            if (ImGui::Button("Flip Vertical"))
            {
                blueprint = colony::game::editor::BlueprintFlipVertical(blueprint);
                setStatus("Blueprint flipped (vertical)");
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            ImGui::Checkbox("Paste includes empty cells (erases plans)", &blueprintPasteIncludeEmpty);

            int anchor = (blueprintAnchor == BlueprintAnchor::TopLeft) ? 0 : 1;
            if (ImGui::Combo("Paste anchor", &anchor, "Top-left\0Center\0"))
            {
                blueprintAnchor = (anchor == 0) ? BlueprintAnchor::TopLeft : BlueprintAnchor::Center;
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Blueprint Library (Disk)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const fs::path bpDir = blueprintDir();
                const std::string bpDirUtf8 = colony::util::PathToUtf8String(bpDir);
                ImGui::TextWrapped("Folder: %s", bpDirUtf8.c_str());

                if (ImGui::Button("Show Folder in Explorer##bp"))
                {
                    const std::wstring w = bpDir.wstring();
                    ::ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                ImGui::SameLine();
                if (ImGui::Button("Refresh##bp"))
                {
                    blueprintLibraryDirty = true;
                }

                ImGui::Spacing();

                ImGui::BeginDisabled(blueprint.Empty());
                ImGui::InputText("Save name", blueprintSaveNameBuf.data(), blueprintSaveNameBuf.size());
                ImGui::SameLine();
                ImGui::Checkbox("Overwrite##bp_overwrite", &blueprintSaveOverwrite);
                ImGui::SameLine();
                if (ImGui::Button("Save current##bp"))
                {
                    std::string err;
                    if (!colony::game::editor::EnsureBlueprintDir(bpDir, &err))
                    {
                        setStatus("Blueprint save failed: " + err, 4.0f);
                    }
                    else
                    {
                        const fs::path outPath = colony::game::editor::BlueprintPathForName(bpDir, blueprintSaveNameBuf.data());
                        std::error_code ec;
                        const bool exists = fs::exists(outPath, ec) && !ec;
                        if (exists && !blueprintSaveOverwrite)
                        {
                            setStatus("Blueprint exists. Enable Overwrite to replace.", 4.0f);
                        }
                        else if (colony::game::editor::SaveBlueprintToFile(blueprint, outPath, &err))
                        {
                            blueprintLibraryDirty = true;
                            setStatus("Blueprint saved: " + colony::util::PathToUtf8String(outPath), 3.0f);
                        }
                        else
                        {
                            setStatus("Blueprint save failed: " + err, 4.0f);
                        }
                    }
                }
                ImGui::EndDisabled();

                if (blueprintLibraryDirty)
                {
                    blueprintLibraryFiles = colony::game::editor::ListBlueprintFiles(bpDir);
                    blueprintLibraryDirty = false;

                    if (blueprintLibrarySelected >= static_cast<int>(blueprintLibraryFiles.size()))
                        blueprintLibrarySelected = blueprintLibraryFiles.empty() ? -1 : 0;

                    blueprintLibraryPreviewName.clear();
                    blueprintLibraryLastError.clear();
                }

                if (blueprintLibraryFiles.empty())
                {
                    ImGui::TextDisabled("No saved blueprints yet.");
                }
                else
                {
                    ImGui::BeginChild("bp_lib_list", ImVec2(0, 120), true);
                    for (int i = 0; i < static_cast<int>(blueprintLibraryFiles.size()); ++i)
                    {
                        const auto& e = blueprintLibraryFiles[i];
                        std::string label = e.name;
                        if (e.modifiedUtcSeconds > 0)
                            label += "  [" + save::FormatLocalTime(e.modifiedUtcSeconds) + "]";

                        if (ImGui::Selectable(label.c_str(), blueprintLibrarySelected == i))
                            blueprintLibrarySelected = i;
                    }
                    ImGui::EndChild();

                    if (blueprintLibrarySelected >= 0 && blueprintLibrarySelected < static_cast<int>(blueprintLibraryFiles.size()))
                    {
                        const auto& sel = blueprintLibraryFiles[blueprintLibrarySelected];
                        const std::string selPathUtf8 = colony::util::PathToUtf8String(sel.path);

                        // Load preview on selection change (or refresh).
                        if (blueprintLibraryPreviewName != selPathUtf8)
                        {
                            blueprintLibraryPreviewName = selPathUtf8;
                            blueprintLibraryLastError.clear();

                            colony::game::editor::PlanBlueprint tmp;
                            std::string err;
                            if (colony::game::editor::LoadBlueprintFromFile(sel.path, tmp, &err))
                            {
                                blueprintLibraryPreview = std::move(tmp);
                            }
                            else
                            {
                                blueprintLibraryPreview.Clear();
                                blueprintLibraryLastError = err;
                            }
                        }

                        ImGui::TextWrapped("Selected: %s", selPathUtf8.c_str());
                        ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(sel.sizeBytes));
                        if (!blueprintLibraryLastError.empty())
                            ImGui::TextWrapped("Preview error: %s", blueprintLibraryLastError.c_str());

                        if (!blueprintLibraryPreview.Empty())
                        {
                            ImGui::Text("Preview: %dx%d", blueprintLibraryPreview.w, blueprintLibraryPreview.h);
                            DrawBlueprintThumbnail(blueprintLibraryPreview, blueprintPasteIncludeEmpty);
                        }

                        if (ImGui::Button("Load selected -> current blueprint##bp"))
                        {
                            std::string err;
                            colony::game::editor::PlanBlueprint tmp;
                            if (colony::game::editor::LoadBlueprintFromFile(sel.path, tmp, &err))
                            {
                                blueprint = std::move(tmp);
                                setStatus("Blueprint loaded: " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
                            }
                            else
                            {
                                setStatus("Blueprint load failed: " + err, 4.0f);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Delete selected##bp"))
                        {
                            const std::string deletedName = sel.name;
                            std::string err;
                            if (colony::game::editor::DeleteBlueprintFile(sel.path, &err))
                            {
                                setStatus("Blueprint deleted: " + deletedName, 3.0f);
                                blueprintLibraryDirty = true;
                                blueprintLibrarySelected = -1;
                                blueprintLibraryPreview.Clear();
                                blueprintLibraryPreviewName.clear();
                            }
                            else
                            {
                                setStatus("Blueprint delete failed: " + err, 4.0f);
                            }
                        }
                    }
                }
            }

            if (ImGui::Button("Select Blueprint tool (9)"))
            {
                tool = Tool::Blueprint;
                setStatus("Tool: Blueprint Paste");
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Minimap", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show minimap", &showMinimap);
            ImGui::SameLine();
            ImGui::SliderInt("Size", &minimapSizePx, 120, 400, "%d px");

            ImGui::Checkbox("Plans", &minimapShowPlans);
            ImGui::SameLine();
            ImGui::Checkbox("Colonists", &minimapShowColonists);
            ImGui::SameLine();
            ImGui::Checkbox("Viewport", &minimapShowViewport);

            if (showMinimap)
            {
                const int worldW = world.width();
                const int worldH = world.height();

                if (worldW > 0 && worldH > 0)
                {
                    const float maxSize = static_cast<float>(minimapSizePx);
                    float mapW = maxSize;
                    float mapH = maxSize;

                    const float aspect = static_cast<float>(worldW) / static_cast<float>(worldH);
                    if (aspect >= 1.f)
                        mapH = maxSize / aspect;
                    else
                        mapW = maxSize * aspect;

                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    const ImVec2 sz = {mapW, mapH};
                    const ImVec2 p1 = {p0.x + mapW, p0.y + mapH};

                    ImGui::InvisibleButton("##minimap", sz, ImGuiButtonFlags_MouseButtonLeft);

                    const bool hovered = ImGui::IsItemHovered();
                    const bool active  = ImGui::IsItemActive();

                    dl->AddRectFilled(p0, p1, IM_COL32(8, 8, 10, 255));
                    dl->AddRect(p0, p1, IM_COL32(70, 70, 80, 255));

                    const int sampleW = std::clamp(static_cast<int>(mapW), 32, 240);
                    const int sampleH = std::clamp(static_cast<int>(mapH), 32, 240);
                    const float cellW = mapW / static_cast<float>(sampleW);
                    const float cellH = mapH / static_cast<float>(sampleH);

                    for (int sy = 0; sy < sampleH; ++sy)
                    {
                        const int wy = (sy * worldH) / sampleH;
                        for (int sx = 0; sx < sampleW; ++sx)
                        {
                            const int wx = (sx * worldW) / sampleW;

                            const proto::Cell& c = world.cell(wx, wy);

                            const ImVec2 a = {p0.x + cellW * static_cast<float>(sx), p0.y + cellH * static_cast<float>(sy)};
                            const ImVec2 b = {a.x + cellW + 0.5f, a.y + cellH + 0.5f};

                            dl->AddRectFilled(a, b, TileFillColor(c.built));

                            if (minimapShowPlans && c.planned != proto::TileType::Empty && c.planned != c.built)
                                dl->AddRectFilled(a, b, TilePlanColor(c.planned));
                        }
                    }

                    // Colonists
                    if (minimapShowColonists)
                    {
                        for (const proto::Colonist& c : world.colonists())
                        {
                            const float u = std::clamp(c.x / static_cast<float>(worldW), 0.f, 1.f);
                            const float v = std::clamp(c.y / static_cast<float>(worldH), 0.f, 1.f);
                            const ImVec2 mp = {p0.x + u * mapW, p0.y + v * mapH};
                            dl->AddCircleFilled(mp, 2.2f, IM_COL32(235, 235, 245, 220));
                        }
                    }

                    // Selection (single tile)
                    if (selectedX >= 0 && selectedY >= 0)
                    {
                        const float u0 = static_cast<float>(selectedX) / static_cast<float>(worldW);
                        const float v0 = static_cast<float>(selectedY) / static_cast<float>(worldH);
                        const float u1 = static_cast<float>(selectedX + 1) / static_cast<float>(worldW);
                        const float v1 = static_cast<float>(selectedY + 1) / static_cast<float>(worldH);
                        dl->AddRect({p0.x + u0 * mapW, p0.y + v0 * mapH}, {p0.x + u1 * mapW, p0.y + v1 * mapH}, IM_COL32(255, 255, 255, 180));
                    }

                    // Selection rectangle
                    if (selectRectHas || selectRectActive)
                    {
                        const int rx0 = std::clamp(std::min(selectRectStartX, selectRectEndX), 0, worldW - 1);
                        const int ry0 = std::clamp(std::min(selectRectStartY, selectRectEndY), 0, worldH - 1);
                        const int rx1 = std::clamp(std::max(selectRectStartX, selectRectEndX), 0, worldW - 1);
                        const int ry1 = std::clamp(std::max(selectRectStartY, selectRectEndY), 0, worldH - 1);

                        const float u0 = static_cast<float>(rx0) / static_cast<float>(worldW);
                        const float v0 = static_cast<float>(ry0) / static_cast<float>(worldH);
                        const float u1 = static_cast<float>(rx1 + 1) / static_cast<float>(worldW);
                        const float v1 = static_cast<float>(ry1 + 1) / static_cast<float>(worldH);

                        dl->AddRect({p0.x + u0 * mapW, p0.y + v0 * mapH}, {p0.x + u1 * mapW, p0.y + v1 * mapH}, IM_COL32(255, 240, 140, 200), 0.f, 0, 2.f);
                    }

                    // Viewport rectangle (approx)
                    if (minimapShowViewport && lastWorldCanvasW > 0.f && lastWorldCanvasH > 0.f)
                    {
                        const auto& cam = camera.State();
                        const float tilePx = 24.f * std::max(DebugCameraController::kMinZoom, cam.zoom);
                        if (tilePx > 0.f)
                        {
                            const float halfW = lastWorldCanvasW / (2.f * tilePx);
                            const float halfH = lastWorldCanvasH / (2.f * tilePx);

                            float minX = cam.panX - halfW;
                            float minY = cam.panY - halfH;
                            float maxX = cam.panX + halfW;
                            float maxY = cam.panY + halfH;

                            minX = std::clamp(minX, 0.f, static_cast<float>(worldW));
                            maxX = std::clamp(maxX, 0.f, static_cast<float>(worldW));
                            minY = std::clamp(minY, 0.f, static_cast<float>(worldH));
                            maxY = std::clamp(maxY, 0.f, static_cast<float>(worldH));

                            const ImVec2 v0 = {p0.x + (minX / static_cast<float>(worldW)) * mapW, p0.y + (minY / static_cast<float>(worldH)) * mapH};
                            const ImVec2 v1 = {p0.x + (maxX / static_cast<float>(worldW)) * mapW, p0.y + (maxY / static_cast<float>(worldH)) * mapH};

                            dl->AddRect(v0, v1, IM_COL32(255, 255, 255, 160), 0.f, 0, 2.f);
                        }
                    }

                    if ((hovered || active) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        const auto& cam = camera.State();

                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        float u = (mp.x - p0.x) / mapW;
                        float v = (mp.y - p0.y) / mapH;

                        u = std::clamp(u, 0.f, 0.9999f);
                        v = std::clamp(v, 0.f, 0.9999f);

                        const int tx = std::clamp(static_cast<int>(u * static_cast<float>(worldW)), 0, worldW - 1);
                        const int ty = std::clamp(static_cast<int>(v * static_cast<float>(worldH)), 0, worldH - 1);

                        const float desiredPanX = static_cast<float>(tx) + 0.5f;
                        const float desiredPanY = static_cast<float>(ty) + 0.5f;

                        camera.ApplyPan(desiredPanX - cam.panX, desiredPanY - cam.panY);
                    }

                    if (hovered)
                        ImGui::SetTooltip("Click/drag to jump the camera.");
                }
                else
                {
                    ImGui::TextDisabled("Minimap unavailable (world size is zero)");
                }
            }
            else
            {
                ImGui::TextDisabled("Minimap hidden");
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("View / Debug");
        ImGui::Checkbox("Brush preview", &showBrushPreview);
        ImGui::Checkbox("Show colonist paths", &showJobPaths);
        ImGui::Checkbox("Show reservations", &showReservations);
        ImGui::Checkbox("Show plan priorities", &showPlanPriorities);
        ImGui::Checkbox("Show rooms overlay", &showRoomsOverlay);
        ImGui::SameLine();
        ImGui::Checkbox("Show room IDs", &showRoomIds);
        ImGui::Text("Indoors: %d rooms, %d tiles", world.indoorsRoomCount(), world.indoorsTileCount());

        ImGui::Separator();
        ImGui::TextUnformatted("Simulation");
        ImGui::Checkbox("Paused (P)", &paused);
        ImGui::SliderFloat("Speed", &simSpeed, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);

        ImGui::Separator();
        ImGui::TextUnformatted("World Reset");
        ImGui::InputInt("Width", &worldResetW);
        ImGui::InputInt("Height", &worldResetH);
        worldResetW = std::clamp(worldResetW, 8, 512);
        worldResetH = std::clamp(worldResetH, 8, 512);
        ImGui::Checkbox("Random seed", &worldResetUseRandomSeed);
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &worldResetSeed);

        ImGui::TextDisabled("Reset uses the values above.");

        ImGui::Separator();
        ImGui::TextUnformatted("Tuning");
        {
            float build = static_cast<float>(world.buildWorkPerSecond);
            float walk  = static_cast<float>(world.colonistWalkSpeed);
            float farmGrowDur    = static_cast<float>(world.farmGrowDurationSeconds);
            float farmYield      = static_cast<float>(world.farmHarvestYieldFood);
            float farmHarvestDur = static_cast<float>(world.farmHarvestDurationSeconds);
            float eat   = static_cast<float>(world.foodPerColonistPerSecond);

            float maxPersonalFood = static_cast<float>(world.colonistMaxPersonalFood);
            float eatThreshold    = static_cast<float>(world.colonistEatThresholdFood);
            float eatDur          = static_cast<float>(world.colonistEatDurationSeconds);

            if (ImGui::SliderFloat("Build work/s", &build, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                world.buildWorkPerSecond = static_cast<double>(build);
            if (ImGui::SliderFloat("Walk speed (tiles/s)", &walk, 0.25f, 12.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                world.colonistWalkSpeed = static_cast<double>(walk);
            ImGui::SeparatorText("Farming");

            if (ImGui::SliderFloat("Grow duration (s)", &farmGrowDur, 1.0f, 180.0f, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
                farmGrowDur = std::max(1.0f, farmGrowDur);
                world.farmGrowDurationSeconds = static_cast<double>(farmGrowDur);
            }
            if (ImGui::SliderFloat("Harvest yield (food)", &farmYield, 0.0f, 50.0f, "%.1f"))
            {
                farmYield = std::max(0.0f, farmYield);
                world.farmHarvestYieldFood = static_cast<double>(farmYield);
            }
            if (ImGui::SliderFloat("Harvest duration (s)", &farmHarvestDur, 0.0f, 10.0f, "%.1f"))
            {
                farmHarvestDur = std::max(0.0f, farmHarvestDur);
                world.farmHarvestDurationSeconds = static_cast<double>(farmHarvestDur);
            }

            if (farmGrowDur > 0.0f && farmYield > 0.0f)
            {
                const int farms = world.builtCount(proto::TileType::Farm);
                const float perFarm = farmYield / farmGrowDur;
                ImGui::TextDisabled("Avg output: %.2f food/s (%.2f per farm)", perFarm * farms, perFarm);
            }
            if (ImGui::SliderFloat("Food/colonist/s", &eat, 0.0f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic))
                world.foodPerColonistPerSecond = static_cast<double>(eat);

            ImGui::SeparatorText("Forestry");

            int yield = world.treeChopYieldWood;
            if (ImGui::SliderInt("Tree chop yield (wood)", &yield, 0, 25))
                world.treeChopYieldWood = std::max(0, yield);

            float attempts = static_cast<float>(world.treeSpreadAttemptsPerSecond);
            if (ImGui::SliderFloat("Tree spread attempts/s", &attempts, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                world.treeSpreadAttemptsPerSecond = std::max(0.0, static_cast<double>(attempts));

            float chance = static_cast<float>(world.treeSpreadChancePerAttempt);
            if (ImGui::SliderFloat("Tree spread chance", &chance, 0.0f, 1.0f, "%.2f"))
                world.treeSpreadChancePerAttempt = std::clamp(static_cast<double>(chance), 0.0, 1.0);

            ImGui::TextDisabled("Demolish trees to gather wood. Trees can slowly regrow on nearby empty tiles.");
            ImGui::TextDisabled("(Regrowth is capped at ~20%% of the map to avoid total overgrowth.)");

            ImGui::SeparatorText("Hunger");

            if (ImGui::SliderFloat("Max personal food", &maxPersonalFood, 0.0f, 20.0f, "%.1f"))
            {
                maxPersonalFood = std::max(0.0f, maxPersonalFood);
                world.colonistMaxPersonalFood = static_cast<double>(maxPersonalFood);

                // Keep the threshold sane when the max shrinks.
                eatThreshold = std::clamp(eatThreshold, 0.0f, maxPersonalFood);
                world.colonistEatThresholdFood = static_cast<double>(eatThreshold);
            }

            if (ImGui::SliderFloat("Eat threshold", &eatThreshold, 0.0f, std::max(0.0f, maxPersonalFood), "%.1f"))
            {
                eatThreshold = std::clamp(eatThreshold, 0.0f, std::max(0.0f, maxPersonalFood));
                world.colonistEatThresholdFood = static_cast<double>(eatThreshold);
            }

            if (ImGui::SliderFloat("Eat duration (s)", &eatDur, 0.0f, 10.0f, "%.1f"))
            {
                eatDur = std::max(0.0f, eatDur);
                world.colonistEatDurationSeconds = static_cast<double>(eatDur);
            }

            if (eat > 0.0f && maxPersonalFood > 0.0f)
            {
                const float fullSec = maxPersonalFood / eat;
                const float atThresholdSec = eatThreshold / eat;
                ImGui::TextDisabled("Full stomach: ~%.0fs", fullSec);
                ImGui::TextDisabled("At threshold: ~%.0fs", atThresholdSec);
            }

            ImGui::SeparatorText("Pathfinding");

            {
                // Algorithm selection
                int algoIdx = (world.pathAlgo == proto::PathAlgo::AStar) ? 0 : 1;
                const char* algoItems[] = { "AStar", "JPS" };
                if (ImGui::Combo("Algorithm", &algoIdx, algoItems, IM_ARRAYSIZE(algoItems)))
                {
                    const proto::PathAlgo newAlgo = (algoIdx == 0) ? proto::PathAlgo::AStar : proto::PathAlgo::JumpPointSearch;
                    world.SetPathAlgo(newAlgo);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(direct orders + repathing)");

                // Path cache knobs
                bool cacheEnabled = world.pathCacheEnabled;
                if (ImGui::Checkbox("Enable path cache", &cacheEnabled))
                    world.SetPathCacheEnabled(cacheEnabled);

                int maxEntries = world.pathCacheMaxEntries;
                if (ImGui::SliderInt("Cache max entries", &maxEntries, 0, 8192, "%d", ImGuiSliderFlags_Logarithmic))
                    world.SetPathCacheMaxEntries(maxEntries);

                // Terrain costs
                bool terrainCosts = world.navUseTerrainCosts;
                if (ImGui::Checkbox("Terrain traversal costs", &terrainCosts))
                    (void)world.SetNavTerrainCostsEnabled(terrainCosts);

                ImGui::TextDisabled("Farms/stockpiles/doors become slightly slower to cross, affecting both movement and path costs.");

                // Stats + maintenance
                const proto::World::PathfindStats stats = world.pathStats();
                const std::size_t cacheSize = world.pathCacheSize();

                if (ImGui::Button("Clear cache"))
                    world.ClearPathCache();
                ImGui::SameLine();
                if (ImGui::Button("Reset stats"))
                    world.ResetPathStats();

                ImGui::Text("Cache: %zu / %d", cacheSize, std::max(0, world.pathCacheMaxEntries));

                const unsigned long long reqTile = static_cast<unsigned long long>(stats.reqTile);
                const unsigned long long reqAdj = static_cast<unsigned long long>(stats.reqAdjacent);
                const unsigned long long hitTile = static_cast<unsigned long long>(stats.hitTile);
                const unsigned long long hitAdj = static_cast<unsigned long long>(stats.hitAdjacent);
                const unsigned long long invalid = static_cast<unsigned long long>(stats.invalidated);
                const unsigned long long evicted = static_cast<unsigned long long>(stats.evicted);
                const unsigned long long astar = static_cast<unsigned long long>(stats.computedAStar);
                const unsigned long long jps = static_cast<unsigned long long>(stats.computedJps);

                const double totalReq = static_cast<double>(reqTile + reqAdj);
                const double totalHit = static_cast<double>(hitTile + hitAdj);
                const double hitRate = (totalReq > 0.0) ? (100.0 * totalHit / totalReq) : 0.0;

                ImGui::Text("Req: tile %llu (hit %llu), adj %llu (hit %llu)", reqTile, hitTile, reqAdj, hitAdj);
                ImGui::Text("Hit rate: %.1f%%", hitRate);
                ImGui::Text("Compute: A* %llu, JPS %llu", astar, jps);
                ImGui::Text("Invalidated: %llu, evicted: %llu", invalid, evicted);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Input Bindings");
        ImGui::Checkbox("Hot Reload", &bindingHotReloadEnabled);
        ImGui::SliderFloat("Poll Interval (s)", &bindingsPollInterval, 0.1f, 5.0f, "%.1f");

        if (bindingsLoadedPath.empty()) {
            ImGui::TextUnformatted("Active: (defaults)");
        } else {
            ImGui::TextWrapped("Active: %s", colony::util::PathToUtf8String(bindingsLoadedPath).c_str());
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

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
