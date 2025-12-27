#include "game/PrototypeGame_Impl.h"

#include "util/PathUtf8.h"

#include "platform/win/PathUtilWin.h"

#include <algorithm>
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
    // TileType is currently 0..4; anything else is treated as Empty.
    if (v <= static_cast<std::uint8_t>(proto::TileType::Stockpile))
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

        if (tool == Tool::Priority)
            ImGui::TextDisabled("Paints the current Brush Priority onto existing plans (no cost).");

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
        ImGui::TextUnformatted("View / Debug");
        ImGui::Checkbox("Brush preview", &showBrushPreview);
        ImGui::Checkbox("Show colonist paths", &showJobPaths);
        ImGui::Checkbox("Show reservations", &showReservations);
        ImGui::Checkbox("Show plan priorities", &showPlanPriorities);

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
            float farm  = static_cast<float>(world.farmFoodPerSecond);
            float eat   = static_cast<float>(world.foodPerColonistPerSecond);

            if (ImGui::SliderFloat("Build work/s", &build, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                world.buildWorkPerSecond = static_cast<double>(build);
            if (ImGui::SliderFloat("Walk speed (tiles/s)", &walk, 0.25f, 12.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                world.colonistWalkSpeed = static_cast<double>(walk);
            if (ImGui::SliderFloat("Farm food/s", &farm, 0.0f, 2.0f, "%.2f"))
                world.farmFoodPerSecond = static_cast<double>(farm);
            if (ImGui::SliderFloat("Food/colonist/s", &eat, 0.0f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic))
                world.foodPerColonistPerSecond = static_cast<double>(eat);
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
