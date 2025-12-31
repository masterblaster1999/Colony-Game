#include "game/PrototypeGame_Impl.h"

#include "util/PathUtf8.h"

#include "platform/win/PathUtilWin.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <system_error>
#include <string_view>
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
    case proto::TileType::Bed: return IM_COL32(150, 85, 150, 255);
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

[[nodiscard]] std::uint8_t ClampPlanPriorityByte(std::uint8_t p) noexcept
{
    // std::clamp requires the same type for value and bounds.
    // planPriority is stored as a byte, so clamp in int-space.
    return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(p), 0, 3));
}

[[nodiscard]] proto::TileType SafeTileTypeFromNibble(std::uint8_t v) noexcept
{
    // TileType is currently 0..8 (up through Bed); anything else is treated as Empty.
    if (v <= static_cast<std::uint8_t>(proto::TileType::Bed))
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

[[nodiscard]] char AsciiToLower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');
    return c;
}

[[nodiscard]] char AsciiToUpper(char c) noexcept
{
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - 'a' + 'A');
    return c;
}

[[nodiscard]] bool StartsWithInsensitive(std::string_view s, std::string_view prefix) noexcept
{
    if (prefix.size() > s.size())
        return false;

    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        if (AsciiToLower(s[i]) != AsciiToLower(prefix[i]))
            return false;
    }

    return true;
}

[[nodiscard]] bool EndsWithInsensitive(std::string_view s, std::string_view suffix) noexcept
{
    if (suffix.size() > s.size())
        return false;

    const std::size_t offset = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i)
    {
        if (AsciiToLower(s[offset + i]) != AsciiToLower(suffix[i]))
            return false;
    }

    return true;
}

[[nodiscard]] bool ContainsInsensitive(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty())
        return true;

    if (needle.size() > haystack.size())
        return false;

    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
    {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
        {
            if (AsciiToLower(haystack[i + j]) != AsciiToLower(needle[j]))
            {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }

    return false;
}

[[nodiscard]] bool IsAsciiDigit(char c) noexcept { return (c >= '0' && c <= '9'); }

[[nodiscard]] std::string ToUpperAscii(std::string_view s) noexcept
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(AsciiToUpper(c));
    return out;
}

[[nodiscard]] bool IsReservedWindowsDeviceName(std::string_view upper) noexcept
{
    // Avoid awkward/confusing filenames on Windows (matches Blueprint sanitizer).
    return upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
           upper == "COM1" || upper == "COM2" || upper == "COM3" || upper == "COM4" ||
           upper == "COM5" || upper == "COM6" || upper == "COM7" || upper == "COM8" ||
           upper == "COM9" || upper == "LPT1" || upper == "LPT2" || upper == "LPT3" ||
           upper == "LPT4" || upper == "LPT5" || upper == "LPT6" || upper == "LPT7" ||
           upper == "LPT8" || upper == "LPT9";
}

[[nodiscard]] bool IsSlotSaveFilename(std::string_view filename, int& outSlot) noexcept
{
    constexpr std::string_view kSlot0 = "proto_world.json";
    if (filename.size() == kSlot0.size() && EndsWithInsensitive(filename, kSlot0))
    {
        outSlot = 0;
        return true;
    }

    // proto_world_slot_<n>.json
    constexpr std::string_view prefix = "proto_world_slot_";
    constexpr std::string_view suffix = ".json";

    if (!StartsWithInsensitive(filename, prefix) || !EndsWithInsensitive(filename, suffix))
        return false;

    const std::size_t begin = prefix.size();
    const std::size_t end   = filename.size() - suffix.size();
    if (end <= begin)
        return false;

    const std::string_view mid = filename.substr(begin, end - begin);

    int value = 0;
    for (char c : mid)
    {
        if (!IsAsciiDigit(c))
            return false;

        value = value * 10 + (c - '0');
    }

    outSlot = value;
    return true;
}

[[nodiscard]] bool IsAutosaveFilename(std::string_view filename, int& outIndex) noexcept
{
    // autosave_<nn>.json
    constexpr std::string_view prefix = "autosave_";
    constexpr std::string_view suffix = ".json";

    if (!StartsWithInsensitive(filename, prefix) || !EndsWithInsensitive(filename, suffix))
        return false;

    const std::size_t begin = prefix.size();
    const std::size_t end   = filename.size() - suffix.size();
    if (end <= begin)
        return false;

    const std::string_view mid = filename.substr(begin, end - begin);
    if (mid.size() != 2)
        return false;

    if (!IsAsciiDigit(mid[0]) || !IsAsciiDigit(mid[1]))
        return false;

    outIndex = (mid[0] - '0') * 10 + (mid[1] - '0');
    return true;
}

[[nodiscard]] bool IsReservedWorldSaveStemUpper(std::string_view upperStem) noexcept
{
    if (upperStem == "PROTO_WORLD")
        return true;

    // PROTO_WORLD_SLOT_<n>
    constexpr std::string_view slotPrefix = "PROTO_WORLD_SLOT_";
    if (StartsWithInsensitive(upperStem, slotPrefix))
    {
        const std::string_view rest = upperStem.substr(slotPrefix.size());
        if (!rest.empty())
        {
            bool allDigits = true;
            for (char c : rest)
            {
                if (!IsAsciiDigit(c))
                {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits)
                return true;
        }
    }

    // AUTOSAVE_<nn>
    constexpr std::string_view autoPrefix = "AUTOSAVE_";
    if (StartsWithInsensitive(upperStem, autoPrefix))
    {
        const std::string_view rest = upperStem.substr(autoPrefix.size());
        if (rest.size() == 2 && IsAsciiDigit(rest[0]) && IsAsciiDigit(rest[1]))
            return true;
    }

    return false;
}

[[nodiscard]] std::string SanitizeSaveName(std::string_view name) noexcept
{
    // Trim whitespace
    std::size_t a = 0;
    std::size_t b = name.size();
    while (a < b && std::isspace(static_cast<unsigned char>(name[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(name[b - 1])))
        --b;

    std::string base(name.substr(a, b - a));

    // Strip extension if the user typed it.
    if (EndsWithInsensitive(base, ".meta.json"))
        base.resize(base.size() - std::string_view(".meta.json").size());
    else if (EndsWithInsensitive(base, ".json"))
        base.resize(base.size() - std::string_view(".json").size());

    // Replace dangerous characters (path separators, quotes, control chars) with underscores.
    std::string out;
    out.reserve(base.size());
    for (char ch : base)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || ch == '-' || ch == '_' || ch == '.' || ch == ' ')
            out.push_back(ch);
        else
            out.push_back('_');
    }

    // Trim trailing dots/spaces (problematic on Windows).
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();

    // Collapse multiple spaces.
    std::string compact;
    compact.reserve(out.size());
    bool prevSpace = false;
    for (char ch : out)
    {
        const bool sp = (ch == ' ');
        if (sp && prevSpace)
            continue;
        compact.push_back(ch);
        prevSpace = sp;
    }

    if (compact.empty())
        compact = "save";

    const std::string upper = ToUpperAscii(compact);
    if (IsReservedWindowsDeviceName(upper))
        compact = "_" + compact;

    if (IsReservedWorldSaveStemUpper(upper))
        compact = "save_" + compact;

    return compact;
}

[[nodiscard]] fs::path NamedWorldSavePathForName(const fs::path& dir, std::string_view userName) noexcept
{
    const std::string base = SanitizeSaveName(userName);
    return dir / (base + ".json");
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
            const float maxPersonalRest = static_cast<float>(std::max(0.0, world.colonistMaxPersonalRest));
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

        if (ImGui::CollapsingHeader("Alerts"))
        {
            ImGui::Checkbox("Enable alerts", &alertsEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Toast overlay", &alertsShowToasts);

            ImGui::Checkbox("Resolve messages", &alertsShowResolveMessages);
            ImGui::SameLine();
            ImGui::Checkbox("Auto pause on critical", &alertsAutoPauseOnCritical);

            // Limits and thresholds
            {
                int maxLog = static_cast<int>(notify.maxLogEntries());
                if (ImGui::SliderInt("Max log entries", &maxLog, 20, 500))
                    notify.setMaxLogEntries(static_cast<std::size_t>(std::max(1, maxLog)));

                int maxToasts = static_cast<int>(notify.maxToasts());
                if (ImGui::SliderInt("Max toasts", &maxToasts, 1, 10))
                    notify.setMaxToasts(static_cast<std::size_t>(std::max(1, maxToasts)));
            }

            ImGui::SliderFloat("Check interval (s)", &alertsCheckIntervalSeconds, 0.1f, 5.0f, "%.1f");
            ImGui::SliderInt("Low wood threshold", &alertsLowWoodThreshold, 0, 200);
            ImGui::SliderFloat("Low food threshold", &alertsLowFoodThreshold, 0.0f, 50.0f, "%.1f");
            ImGui::SliderFloat("Starving personal food", &alertsStarvingThreshold, 0.0f, 2.0f, "%.2f");

            if (ImGui::Button("Clear log"))
                notify.clearLog();
            ImGui::SameLine();
            if (ImGui::Button("Clear toasts"))
                notify.clearToasts();
            ImGui::SameLine();
            if (ImGui::Button("Test toast"))
                pushNotificationAutoToast(util::NotifySeverity::Info, "Test notification");

            const auto& log = notify.log();
            ImGui::TextDisabled("Messages: %d", static_cast<int>(log.size()));

            ImGui::BeginChild("##notify_log", ImVec2(0, 180), true);

            const ImVec4 colInfo{1.0f, 1.0f, 1.0f, 0.90f};
            const ImVec4 colWarn{1.0f, 0.80f, 0.30f, 1.00f};
            const ImVec4 colErr {1.0f, 0.35f, 0.35f, 1.00f};

            for (int i = static_cast<int>(log.size()) - 1; i >= 0; --i)
            {
                const util::NotificationEntry& e = log[static_cast<std::size_t>(i)];

                const int t = static_cast<int>(std::max(0.0, e.timeSeconds));
                const int mm = t / 60;
                const int ss = t % 60;

                const ImVec4 c = (e.severity == util::NotifySeverity::Error)
                    ? colErr
                    : (e.severity == util::NotifySeverity::Warning)
                        ? colWarn
                        : colInfo;

                ImGui::PushID(i);

                if (e.target.kind != util::NotifyTarget::Kind::None)
                {
                    if (ImGui::SmallButton("Focus"))
                        focusNotificationTarget(e.target);
                    ImGui::SameLine();
                }

                ImGui::TextColored(c,
                                   "%02d:%02d [%s] %s",
                                   mm,
                                   ss,
                                   util::NotifySeverityName(e.severity),
                                   e.text.c_str());

                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        if (ImGui::CollapsingHeader("Colonists"))
        {
            ImGui::TextDisabled("Inspect tool: left-click selects a primary colonist; Ctrl+click toggles multi-select.\n"
                                "Drafted colonists ignore auto build/harvest.\n"
                                "While drafted: right-click orders Move (all selected) / Build+Harvest (primary). Shift+right-click queues.");

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
            if (ImGui::BeginTable("colonists_table", 12, flags, ImVec2(0, tableH)))
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
                ImGui::TableSetupColumn("Rest");
                ImGui::TableSetupColumn("Pos");
                ImGui::TableSetupColumn("Actions");
                ImGui::TableHeadersRow();

                for (auto& c : world.colonists())
                {
                    ImGui::TableNextRow();

                    // Select (multi-select)
                    ImGui::TableNextColumn();
                    {
                        bool inSel = isColonistInSelection(c.id);
                        char selId[32];
                        std::snprintf(selId, sizeof(selId), "##sel_%d", c.id);

                        if (ImGui::Checkbox(selId, &inSel))
                        {
                            if (inSel)
                                addColonistToSelection(c.id, /*makePrimary=*/ (selectedColonistId < 0));
                            else
                                removeColonistFromSelection(c.id);
                        }

                        ImGui::SameLine();

                        char idLabel[16];
                        std::snprintf(idLabel, sizeof(idLabel), "C%02d", c.id);

                        const bool isPrimary = (c.id == selectedColonistId);
                        if (ImGui::Selectable(idLabel, isPrimary))
                        {
                            addColonistToSelection(c.id, /*makePrimary=*/ true);
                            selectedX = static_cast<int>(std::floor(c.x));
                            selectedY = static_cast<int>(std::floor(c.y));
                        }
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
                        case proto::Colonist::JobKind::Sleep: job = "Sleeping"; break;
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

                    // Rest
                    ImGui::TableNextColumn();
                    if (maxPersonalRest > 0.0f)
                        ImGui::Text("%.1f / %.1f", c.personalRest, maxPersonalRest);
                    else
                        ImGui::Text("%.1f", c.personalRest);

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

// Selection (multi-select)
if (ImGui::CollapsingHeader("Selection"))
{
    ImGui::Text("Selected: %d colonist(s)", static_cast<int>(selectedColonistIds.size()));

    if (selectedColonistIds.empty())
    {
        ImGui::TextDisabled("Tip: In the world (Inspect tool), Ctrl+Left-click colonists to multi-select.");
    }
    else
    {
        // Quick ID display.
        std::string ids;
        for (std::size_t i = 0; i < selectedColonistIds.size(); ++i)
        {
            if (i > 0)
                ids += ", ";
            ids += "C" + (selectedColonistIds[i] < 10 ? std::string("0") : std::string("")) +
                   std::to_string(selectedColonistIds[i]);
        }
        ImGui::TextDisabled("IDs: %s", ids.c_str());

        if (ImGui::SmallButton("Clear selection"))
        {
            clearColonistSelection();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Select all"))
        {
            selectedColonistIds.clear();
            selectedColonistIds.reserve(world.colonists().size());
            for (const auto& c : world.colonists())
                selectedColonistIds.push_back(c.id);
            // Keep primary stable-ish (first).
            selectedColonistId = selectedColonistIds.empty() ? -1 : selectedColonistIds.front();
            normalizeColonistSelection();
        }

        // Focus group: pan camera to average position.
        ImGui::SameLine();
        if (ImGui::SmallButton("Focus group"))
        {
            float ax = 0.0f;
            float ay = 0.0f;
            int   n  = 0;
            for (const auto& c : world.colonists())
            {
                if (!isColonistInSelection(c.id))
                    continue;
                ax += c.x;
                ay += c.y;
                ++n;
            }

            if (n > 0)
            {
                ax /= static_cast<float>(n);
                ay /= static_cast<float>(n);

                const DebugCameraState& s = camera.State();
                (void)camera.ApplyPan(ax - s.panX, ay - s.panY);
            }
        }

        if (ImGui::SmallButton("Draft selected"))
        {
            for (const int cid : selectedColonistIds)
                world.SetColonistDrafted(cid, true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Undraft selected"))
        {
            for (const int cid : selectedColonistIds)
                world.SetColonistDrafted(cid, false);
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel jobs"))
        {
            for (const int cid : selectedColonistIds)
                (void)world.CancelColonistJob(cid);
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Clear queues"))
        {
            for (auto& c : world.colonists())
                if (isColonistInSelection(c.id))
                    c.manualQueue.clear();
        }

        // Group role assignment.
        RoleId commonRole = RoleId::Worker;
        bool mixed = false;
        bool first = true;
        for (const auto& c : world.colonists())
        {
            if (!isColonistInSelection(c.id))
                continue;

            if (first)
            {
                commonRole = c.role.role;
                first = false;
            }
            else if (c.role.role != commonRole)
            {
                mixed = true;
                break;
            }
        }

        const char* preview = mixed ? "<mixed>" : RoleDefOf(commonRole).name;
        if (ImGui::BeginCombo("Role (selected)", preview))
        {
            for (int i = 0; i < static_cast<int>(RoleId::Count); ++i)
            {
                const RoleId rid = static_cast<RoleId>(i);
                const bool selected = (!mixed && rid == commonRole);

                if (ImGui::Selectable(RoleDefOf(rid).name, selected))
                {
                    for (const int cid : selectedColonistIds)
                        (void)world.SetColonistRole(cid, rid);
                }

                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("Tip: Move orders apply to all selected colonists.\n"
                            "Build/Harvest orders apply to the primary selection only.");
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

            auto beginDisabledIf = [&](bool disabled) {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18400
                if (disabled)
                    ImGui::BeginDisabled(true);
#else
                if (disabled)
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
#endif
            };

            auto endDisabledIf = [&](bool disabled) {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18400
                if (disabled)
                    ImGui::EndDisabled();
#else
                if (disabled)
                    ImGui::PopStyleVar();
#endif
            };

            // Create a named/manual save (writes a regular .json alongside a tiny .meta.json).
            ImGui::TextDisabled("Create named save");
            ImGui::InputText("Name##named_save", namedSaveNameBuf.data(), namedSaveNameBuf.size());
            ImGui::SameLine();
            ImGui::Checkbox("Overwrite##named_save_over", &namedSaveOverwrite);

            const fs::path namedPreview = NamedWorldSavePathForName(worldSaveDir(), namedSaveNameBuf.data());
            ImGui::TextWrapped("Path: %s", colony::util::PathToUtf8String(namedPreview).c_str());

            if (ImGui::Button("Save As##named_save"))
            {
                const std::string nm = namedSaveNameBuf.data();
                if (nm.empty())
                {
                    setStatus("Enter a save name", 2.0f);
                }
                else
                {
                    std::error_code ec;
                    const bool exists = fs::exists(namedPreview, ec) && !ec;
                    if (exists && !namedSaveOverwrite)
                    {
                        setStatus("Save already exists (enable Overwrite)", 3.0f);
                    }
                    else
                    {
                        (void)saveWorldToPath(namedPreview, /*showStatus=*/true);
                        saveBrowserDirty = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear##named_save_clear"))
            {
                namedSaveNameBuf[0] = '\0';
            }

            ImGui::Separator();

            // Browser options
            ImGui::InputText("Filter##savebrowser_filter", saveBrowserFilterBuf.data(), saveBrowserFilterBuf.size());
            ImGui::SameLine();
            ImGui::Checkbox("Slots##sb_slots", &saveBrowserShowSlots);
            ImGui::SameLine();
            ImGui::Checkbox("Autosaves##sb_autosaves", &saveBrowserShowAutosaves);
            ImGui::SameLine();
            ImGui::Checkbox("Named##sb_named", &saveBrowserShowNamed);

            const char* sortItems[] = {"Kind", "Time (newest)", "Name"};
            ImGui::Combo("Sort##sb_sort", &saveBrowserSortMode, sortItems, IM_ARRAYSIZE(sortItems));

            auto refreshSaveBrowser = [&]() {
                // Attempt to preserve selection by path across refreshes.
                fs::path prevPath;
                if (saveBrowserSelected >= 0 && saveBrowserSelected < static_cast<int>(saveBrowserEntries.size()))
                    prevPath = saveBrowserEntries[saveBrowserSelected].path;

                saveBrowserEntries.clear();
                saveBrowserSelected         = -1;
                saveBrowserPendingDelete    = -1;
                saveBrowserPendingDeleteTtl = 0.f;

                const fs::path dir = worldSaveDir();

                auto fillEntry = [&](SaveBrowserEntry& e) {
                    std::error_code ec;

                    e.exists = fs::exists(e.path, ec) && !ec;
                    ec.clear();
                    e.metaExists = fs::exists(e.metaPath, ec) && !ec;

                    // Size (world file only)
                    e.sizeBytes = 0;
                    if (e.exists)
                    {
                        ec.clear();
                        e.sizeBytes = fs::file_size(e.path, ec);
                        if (ec)
                            e.sizeBytes = 0;
                    }

                    // Meta read (fast; avoids parsing the full world JSON)
                    e.metaOk = false;
                    e.metaError.clear();
                    e.summary = save::SaveSummary{};
                    if (e.metaExists)
                    {
                        std::string err;
                        e.metaOk = save::ReadMetaFile(e.metaPath, e.summary, &err);
                        if (!e.metaOk)
                            e.metaError = err;
                    }

                    // Best-effort timestamp for list sorting/display.
                    // Prefer meta's savedUnixSecondsUtc; fall back to last_write_time().
                    e.displayUnixSecondsUtc = 0;
                    e.timeFromMeta          = false;
                    if (e.metaOk && e.summary.savedUnixSecondsUtc > 0)
                    {
                        e.displayUnixSecondsUtc = e.summary.savedUnixSecondsUtc;
                        e.timeFromMeta          = true;
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
                };

                // Slots (always show 0..9)
                for (int slot = 0; slot <= 9; ++slot)
                {
                    SaveBrowserEntry e;
                    e.kind = SaveBrowserEntry::Kind::Slot;
                    e.index = slot;
                    e.displayName.clear();
                    e.path = worldSavePathForSlot(slot);
                    e.metaPath = save::MetaPathFor(e.path);
                    fillEntry(e);
                    saveBrowserEntries.push_back(std::move(e));
                }

                // Autosaves (show 00..19; include meta-only entries so users can clean up orphaned meta files)
                for (int i = 0; i < 20; ++i)
                {
                    char buf[32] = {};
                    std::snprintf(buf, sizeof(buf), "autosave_%02d.json", i);

                    SaveBrowserEntry e;
                    e.kind = SaveBrowserEntry::Kind::Autosave;
                    e.index = i;
                    e.displayName.clear();
                    e.path = dir / buf;
                    e.metaPath = save::MetaPathFor(e.path);
                    fillEntry(e);

                    if (!e.exists && !e.metaExists)
                        continue;

                    saveBrowserEntries.push_back(std::move(e));
                }

                // Named saves: any other "*.json" in the save directory (excluding "*.meta.json")
                {
                    std::error_code dec;
                    if (fs::exists(dir, dec) && !dec)
                    {
                        for (const fs::directory_entry& de : fs::directory_iterator(dir, dec))
                        {
                            if (dec)
                                break;

                            if (!de.is_regular_file(dec) || dec)
                                continue;

                            const fs::path p = de.path();
                            const std::string fname = colony::util::PathToUtf8String(p.filename());

                            if (!EndsWithInsensitive(fname, ".json"))
                                continue;

                            if (EndsWithInsensitive(fname, ".meta.json"))
                                continue;

                            int tmp = 0;
                            if (IsSlotSaveFilename(fname, tmp))
                                continue;

                            if (IsAutosaveFilename(fname, tmp))
                                continue;

                            SaveBrowserEntry e;
                            e.kind = SaveBrowserEntry::Kind::Named;
                            e.index = -1;
                            e.path = p;
                            e.metaPath = save::MetaPathFor(e.path);
                            e.displayName = colony::util::PathToUtf8String(p.stem());
                            fillEntry(e);

                            if (!e.exists && !e.metaExists)
                                continue;

                            saveBrowserEntries.push_back(std::move(e));
                        }
                    }
                }

                // Restore selection if possible.
                if (!prevPath.empty())
                {
                    for (int i = 0; i < static_cast<int>(saveBrowserEntries.size()); ++i)
                    {
                        if (saveBrowserEntries[i].path == prevPath)
                        {
                            saveBrowserSelected = i;
                            break;
                        }
                    }
                }

                // Default to first existing world file if nothing is selected.
                if (saveBrowserSelected < 0)
                {
                    for (int i = 0; i < static_cast<int>(saveBrowserEntries.size()); ++i)
                    {
                        if (saveBrowserEntries[i].exists)
                        {
                            saveBrowserSelected = i;
                            break;
                        }
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

            auto formatBytes = [&](std::uintmax_t bytes) -> std::string {
                const double b = static_cast<double>(bytes);
                const double kb = 1024.0;
                const double mb = kb * 1024.0;
                const double gb = mb * 1024.0;
                char buf[64] = {};
                if (b >= gb)
                    std::snprintf(buf, sizeof(buf), "%.2f GiB", b / gb);
                else if (b >= mb)
                    std::snprintf(buf, sizeof(buf), "%.2f MiB", b / mb);
                else if (b >= kb)
                    std::snprintf(buf, sizeof(buf), "%.1f KiB", b / kb);
                else
                    std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
                return buf;
            };

            auto buildBaseLabel = [&](const SaveBrowserEntry& e) -> std::string {
                switch (e.kind)
                {
                case SaveBrowserEntry::Kind::Slot:
                    return (e.index == 0) ? std::string("Slot 0 (Main)") : (std::string("Slot ") + std::to_string(e.index));
                case SaveBrowserEntry::Kind::Autosave:
                    {
                        char buf[32] = {};
                        std::snprintf(buf, sizeof(buf), "Autosave %02d", e.index);
                        return buf;
                    }
                case SaveBrowserEntry::Kind::Named:
                    return e.displayName.empty() ? std::string("Named Save") : (std::string("Named: ") + e.displayName);
                }
                return std::string("Save");
            };

            auto buildListLabel = [&](const SaveBrowserEntry& e) -> std::string {
                std::string label = buildBaseLabel(e);

                if (e.displayUnixSecondsUtc > 0)
                {
                    label += "  ";
                    label += save::FormatLocalTime(e.displayUnixSecondsUtc);
                }

                if (e.exists)
                {
                    label += "  (";
                    label += formatBytes(e.sizeBytes);
                    label += ")";
                }

                if (!e.exists)
                    label += "  [missing]";

                if (e.metaExists && !e.metaOk)
                    label += "  [bad meta]";

                return label;
            };

            // Filter and sort the visible list without mutating the underlying storage.
            std::vector<int> visible;
            visible.reserve(saveBrowserEntries.size());

            const std::string filter = saveBrowserFilterBuf.data();
            for (int i = 0; i < static_cast<int>(saveBrowserEntries.size()); ++i)
            {
                const SaveBrowserEntry& e = saveBrowserEntries[i];

                if (e.kind == SaveBrowserEntry::Kind::Slot && !saveBrowserShowSlots)
                    continue;
                if (e.kind == SaveBrowserEntry::Kind::Autosave && !saveBrowserShowAutosaves)
                    continue;
                if (e.kind == SaveBrowserEntry::Kind::Named && !saveBrowserShowNamed)
                    continue;

                if (!filter.empty())
                {
                    const std::string base = buildBaseLabel(e);
                    const std::string fname = colony::util::PathToUtf8String(e.path.filename());
                    if (!ContainsInsensitive(base, filter) && !ContainsInsensitive(fname, filter))
                        continue;
                }

                visible.push_back(i);
            }

            // Ensure selection remains visible.
            if (saveBrowserSelected >= 0)
            {
                bool selVis = false;
                for (int idx : visible)
                {
                    if (idx == saveBrowserSelected)
                    {
                        selVis = true;
                        break;
                    }
                }
                if (!selVis && !visible.empty())
                    saveBrowserSelected = visible[0];
            }
            else if (!visible.empty())
            {
                saveBrowserSelected = visible[0];
            }

            auto kindOrder = [&](SaveBrowserEntry::Kind k) -> int {
                switch (k)
                {
                case SaveBrowserEntry::Kind::Slot: return 0;
                case SaveBrowserEntry::Kind::Autosave: return 1;
                case SaveBrowserEntry::Kind::Named: return 2;
                }
                return 3;
            };

            auto ciLess = [&](const std::string& a, const std::string& b) {
                const std::size_t n = std::min(a.size(), b.size());
                for (std::size_t i = 0; i < n; ++i)
                {
                    const char ca = AsciiToLower(a[i]);
                    const char cb = AsciiToLower(b[i]);
                    if (ca < cb) return true;
                    if (ca > cb) return false;
                }
                return a.size() < b.size();
            };

            if (saveBrowserSortMode == 1)
            {
                // Time (newest first)
                std::sort(visible.begin(), visible.end(), [&](int ia, int ib) {
                    const SaveBrowserEntry& a = saveBrowserEntries[ia];
                    const SaveBrowserEntry& b = saveBrowserEntries[ib];

                    if (a.displayUnixSecondsUtc != b.displayUnixSecondsUtc)
                        return a.displayUnixSecondsUtc > b.displayUnixSecondsUtc;

                    const int ka = kindOrder(a.kind);
                    const int kb = kindOrder(b.kind);
                    if (ka != kb) return ka < kb;

                    return ciLess(buildBaseLabel(a), buildBaseLabel(b));
                });
            }
            else if (saveBrowserSortMode == 2)
            {
                // Name
                std::sort(visible.begin(), visible.end(), [&](int ia, int ib) {
                    const SaveBrowserEntry& a = saveBrowserEntries[ia];
                    const SaveBrowserEntry& b = saveBrowserEntries[ib];

                    const std::string la = buildBaseLabel(a);
                    const std::string lb = buildBaseLabel(b);
                    if (la != lb)
                        return ciLess(la, lb);

                    return a.displayUnixSecondsUtc > b.displayUnixSecondsUtc;
                });
            }
            else
            {
                // Kind (stable grouping)
                std::sort(visible.begin(), visible.end(), [&](int ia, int ib) {
                    const SaveBrowserEntry& a = saveBrowserEntries[ia];
                    const SaveBrowserEntry& b = saveBrowserEntries[ib];

                    const int ka = kindOrder(a.kind);
                    const int kb = kindOrder(b.kind);
                    if (ka != kb) return ka < kb;

                    if (a.kind != SaveBrowserEntry::Kind::Named)
                        return a.index < b.index;

                    return ciLess(a.displayName, b.displayName);
                });
            }

            ImGui::BeginChild("##savebrowser_list", ImVec2(0, 240), true);
            for (int idx : visible)
            {
                const SaveBrowserEntry& e = saveBrowserEntries[idx];
                const std::string label = buildListLabel(e);
                if (ImGui::Selectable(label.c_str(), saveBrowserSelected == idx))
                    saveBrowserSelected = idx;
            }
            ImGui::EndChild();

            if (saveBrowserSelected >= 0 && saveBrowserSelected < static_cast<int>(saveBrowserEntries.size()))
            {
                SaveBrowserEntry& e = saveBrowserEntries[saveBrowserSelected];

                if (saveBrowserLastSelected != saveBrowserSelected)
                {
                    saveBrowserLastSelected = saveBrowserSelected;

                    if (e.kind == SaveBrowserEntry::Kind::Named)
                    {
                        std::snprintf(saveBrowserRenameBuf.data(), saveBrowserRenameBuf.size(), "%s", e.displayName.c_str());
                    }
                    else
                    {
                        saveBrowserRenameBuf[0] = '\0';
                    }

                    // Reset copy-name suggestion for the new selection.
                    const std::string base = (e.kind == SaveBrowserEntry::Kind::Named)
                                               ? e.displayName
                                               : buildBaseLabel(e);
                    std::snprintf(saveBrowserCopyNameBuf.data(), saveBrowserCopyNameBuf.size(), "%s copy", base.c_str());
                }

                ImGui::Separator();
                ImGui::TextWrapped("Selected path: %s", colony::util::PathToUtf8String(e.path).c_str());
                ImGui::TextDisabled("%s", buildBaseLabel(e).c_str());

                if (e.metaOk)
                {
                    const std::string summaryLine = save::FormatSummaryLine(e.summary);
                    ImGui::TextWrapped("%s", summaryLine.c_str());
                    if (e.displayUnixSecondsUtc > 0)
                        ImGui::TextDisabled("Saved: %s%s", save::FormatLocalTime(e.displayUnixSecondsUtc).c_str(), e.timeFromMeta ? " (meta)" : " (file)");

                    DrawSaveThumbnail(e.summary);
                }
                else if (e.metaExists)
                {
                    ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "Meta file error: %s", e.metaError.c_str());
                }
                else
                {
                    ImGui::TextDisabled("No meta file yet. Make a new save to generate one.");
                }

                if (e.exists)
                    ImGui::TextDisabled("World size: %s", formatBytes(e.sizeBytes).c_str());
                else
                    ImGui::TextDisabled("World file missing.");

                // Primary actions
                beginDisabledIf(!e.exists);
                if (ImGui::Button("Load Selected"))
                    (void)loadWorldFromPath(e.path, /*showStatus=*/true);
                endDisabledIf(!e.exists);

                ImGui::SameLine();
                if (ImGui::Button("Show in Explorer##savebrowser_selected"))
                {
                    const std::wstring w = e.path.wstring();
                    std::wstring args = L"/select,\"" + w + L"\"";
                    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                }

                // Delete with a short confirmation window.
                ImGui::SameLine();
                if (saveBrowserPendingDelete == saveBrowserSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 60, 60, 255));
                    if (ImGui::Button("Confirm Delete"))
                    {
                        std::error_code ec;
                        bool ok = true;

                        if (e.exists)
                            ok &= winpath::remove_with_retry(e.path, &ec, /*max_attempts=*/64);

                        if (e.metaExists)
                            ok &= winpath::remove_with_retry(e.metaPath, &ec, /*max_attempts=*/64);

                        if (ok)
                        {
                            setStatus("Deleted save", 2.0f);
                            pushNotificationAutoToast(util::NotifySeverity::Info, "Deleted save: " + colony::util::PathToUtf8String(e.path.filename()));
                        }
                        else
                        {
                            setStatus("Delete failed: " + ec.message(), 4.0f);
                            pushNotificationAutoToast(util::NotifySeverity::Warning, "Delete failed: " + ec.message());
                        }

                        saveBrowserPendingDelete = -1;
                        saveBrowserPendingDeleteTtl = 0.f;
                        saveBrowserSelected = -1;
                        saveBrowserLastSelected = -1;
                        saveBrowserDirty = true;
                        refreshSaveBrowser();
                    }
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        saveBrowserPendingDelete = -1;
                        saveBrowserPendingDeleteTtl = 0.f;
                    }
                }
                else
                {
                    if (ImGui::Button("Delete##savebrowser_delete"))
                    {
                        saveBrowserPendingDelete = saveBrowserSelected;
                        saveBrowserPendingDeleteTtl = 4.f;
                    }
                }

                // Copy / Rename
                ImGui::Spacing();
                ImGui::TextDisabled("Copy / Rename");

                // Rename (Named saves only)
                {
                    const bool canRename = (e.kind == SaveBrowserEntry::Kind::Named);

                    beginDisabledIf(!canRename);
                    ImGui::InputText("Rename to##savebrowser_rename", saveBrowserRenameBuf.data(), saveBrowserRenameBuf.size());
                    ImGui::SameLine();
                    ImGui::Checkbox("Overwrite##savebrowser_rename_over", &saveBrowserRenameOverwrite);

                    if (ImGui::Button("Rename##savebrowser_rename_btn"))
                    {
                        const std::string newName = saveBrowserRenameBuf.data();
                        if (newName.empty())
                        {
                            setStatus("Enter a new name", 2.0f);
                        }
                        else
                        {
                            const fs::path dst = NamedWorldSavePathForName(worldSaveDir(), newName);
                            if (dst == e.path)
                            {
                                setStatus("Name unchanged", 1.5f);
                            }
                            else
                            {
                                std::error_code ec;
                                const bool dstExists = fs::exists(dst, ec) && !ec;
                                if (dstExists && !saveBrowserRenameOverwrite)
                                {
                                    setStatus("Target exists (enable Overwrite)", 3.0f);
                                }
                                else
                                {
                                    const fs::path metaSrc = e.metaPath;
                                    const fs::path metaDst = save::MetaPathFor(dst);

                                    if (dstExists && saveBrowserRenameOverwrite)
                                    {
                                        (void)winpath::remove_with_retry(dst, &ec, /*max_attempts=*/64);
                                        (void)winpath::remove_with_retry(metaDst, &ec, /*max_attempts=*/64);
                                    }

                                    ec.clear();
                                    const bool ok = winpath::rename_with_retry(e.path, dst, &ec, /*max_attempts=*/64);
                                    if (ok)
                                    {
                                        // Update in-memory entry so refresh preserves selection.
                                        e.path = dst;
                                        e.metaPath = metaDst;
                                        e.displayName = colony::util::PathToUtf8String(dst.stem());

                                        if (e.metaExists)
                                        {
                                            std::error_code mec;
                                            const bool metaOk = winpath::rename_with_retry(metaSrc, metaDst, &mec, /*max_attempts=*/64);
                                            if (!metaOk)
                                            {
                                                pushNotificationAutoToast(util::NotifySeverity::Warning,
                                                                          "Renamed save, but meta rename failed: " + mec.message());
                                            }
                                        }

                                        setStatus("Renamed save", 2.0f);
                                        pushNotificationAutoToast(util::NotifySeverity::Info,
                                                                  "Renamed save to: " + colony::util::PathToUtf8String(dst.filename()));
                                        saveBrowserDirty = true;
                                    }
                                    else
                                    {
                                        setStatus("Rename failed: " + ec.message(), 4.0f);
                                        pushNotificationAutoToast(util::NotifySeverity::Warning, "Rename failed: " + ec.message());
                                    }
                                }
                            }
                        }
                    }
                    endDisabledIf(!canRename);
                }

                // Copy selected -> slot
                {
                    beginDisabledIf(!e.exists);
                    ImGui::InputInt("Target slot##savebrowser_copy_slot", &saveBrowserCopyToSlot);
                    saveBrowserCopyToSlot = std::clamp(saveBrowserCopyToSlot, 0, 9);
                    ImGui::SameLine();
                    ImGui::Checkbox("Overwrite##savebrowser_copy_slot_over", &saveBrowserCopyOverwrite);

                    if (ImGui::Button("Copy to Slot##savebrowser_copy_slot_btn"))
                    {
                        const fs::path dst = worldSavePathForSlot(saveBrowserCopyToSlot);
                        const fs::path metaDst = save::MetaPathFor(dst);

                        std::error_code ec;
                        const bool dstExists = fs::exists(dst, ec) && !ec;

                        if (dstExists && !saveBrowserCopyOverwrite)
                        {
                            setStatus("Target slot exists (enable Overwrite)", 3.0f);
                        }
                        else
                        {
                            ec.clear();
                            const bool ok = winpath::copy_file_with_retry(e.path, dst, /*overwrite_existing=*/saveBrowserCopyOverwrite, &ec, /*max_attempts=*/64);
                            if (ok)
                            {
                                // Copy meta if available; otherwise remove stale destination meta when overwriting.
                                if (e.metaExists)
                                {
                                    std::error_code mec;
                                    (void)winpath::copy_file_with_retry(e.metaPath, metaDst, /*overwrite_existing=*/true, &mec, /*max_attempts=*/64);
                                }
                                else if (saveBrowserCopyOverwrite)
                                {
                                    std::error_code mec;
                                    (void)winpath::remove_with_retry(metaDst, &mec, /*max_attempts=*/32);
                                }

                                setStatus("Copied save to slot " + std::to_string(saveBrowserCopyToSlot), 2.0f);
                                pushNotificationAutoToast(util::NotifySeverity::Info,
                                                          "Copied save to slot " + std::to_string(saveBrowserCopyToSlot));
                                saveBrowserDirty = true;
                            }
                            else
                            {
                                setStatus("Copy failed: " + ec.message(), 4.0f);
                                pushNotificationAutoToast(util::NotifySeverity::Warning, "Copy failed: " + ec.message());
                            }
                        }
                    }
                    endDisabledIf(!e.exists);
                }

                // Copy selected -> named save
                {
                    beginDisabledIf(!e.exists);
                    ImGui::InputText("Copy name##savebrowser_copy_name", saveBrowserCopyNameBuf.data(), saveBrowserCopyNameBuf.size());
                    ImGui::SameLine();
                    ImGui::Checkbox("Overwrite##savebrowser_copy_name_over", &saveBrowserCopyNameOverwrite);

                    if (ImGui::Button("Copy to Named Save##savebrowser_copy_name_btn"))
                    {
                        const std::string nm = saveBrowserCopyNameBuf.data();
                        if (nm.empty())
                        {
                            setStatus("Enter a name", 2.0f);
                        }
                        else
                        {
                            const fs::path dst = NamedWorldSavePathForName(worldSaveDir(), nm);
                            const fs::path metaDst = save::MetaPathFor(dst);

                            std::error_code ec;
                            const bool dstExists = fs::exists(dst, ec) && !ec;

                            if (dst == e.path)
                            {
                                setStatus("Target is the selected file", 2.0f);
                            }
                            else if (dstExists && !saveBrowserCopyNameOverwrite)
                            {
                                setStatus("Target exists (enable Overwrite)", 3.0f);
                            }
                            else
                            {
                                ec.clear();
                                const bool ok = winpath::copy_file_with_retry(e.path, dst, /*overwrite_existing=*/saveBrowserCopyNameOverwrite, &ec, /*max_attempts=*/64);
                                if (ok)
                                {
                                    if (e.metaExists)
                                    {
                                        std::error_code mec;
                                        (void)winpath::copy_file_with_retry(e.metaPath, metaDst, /*overwrite_existing=*/true, &mec, /*max_attempts=*/64);
                                    }
                                    else if (saveBrowserCopyNameOverwrite)
                                    {
                                        std::error_code mec;
                                        (void)winpath::remove_with_retry(metaDst, &mec, /*max_attempts=*/32);
                                    }

                                    setStatus("Copied save", 2.0f);
                                    pushNotificationAutoToast(util::NotifySeverity::Info,
                                                              "Copied save to: " + colony::util::PathToUtf8String(dst.filename()));
                                    saveBrowserDirty = true;
                                }
                                else
                                {
                                    setStatus("Copy failed: " + ec.message(), 4.0f);
                                    pushNotificationAutoToast(util::NotifySeverity::Warning, "Copy failed: " + ec.message());
                                }
                            }
                        }
                    }
                    endDisabledIf(!e.exists);
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
        toolRadio(Tool::Door, "D  Door", proto::TileType::Door);
        toolRadio(Tool::Bed, "B  Bed", proto::TileType::Bed);
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

        if (ImGui::Checkbox("Atomic placement (full brush / rect / blueprint)", &atomicPlanPlacement))
        {
            setStatus(std::string("Atomic placement ") + (atomicPlanPlacement ? "enabled" : "disabled"), 1.5f);
        }
        ImGui::TextDisabled("When enabled, batch plan edits either fully apply or do nothing if you lack wood.");

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
            ImGui::Checkbox("Trim empty borders on copy", &blueprintCopyTrimEmptyBorders);

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
                                    pr = ClampPlanPriorityByte(c.planPriority);
                                }
                            }
                            else
                            {
                                if (hasActivePlan)
                                {
                                    t  = c.planned;
                                    pr = ClampPlanPriorityByte(c.planPriority);
                                }
                                else
                                {
                                    t  = c.built;
                                    pr = 0;
                                }
                            }

                            // Blueprints are for plans; clamp out non-plan tiles (e.g. Trees).
                            if (t == proto::TileType::Tree)
                                t = proto::TileType::Empty;

                            if (t != proto::TileType::Empty)
                                ++nonEmpty;

                            blueprint.packed[static_cast<std::size_t>(y * bw + x)] = colony::game::editor::BlueprintPack(t, pr);
                        }
                    }

                    if (blueprintCopyTrimEmptyBorders)
                    {
                        const int oldW = blueprint.w;
                        const int oldH = blueprint.h;

                        blueprint = colony::game::editor::BlueprintTrimEmptyBorders(blueprint);

                        if (blueprint.Empty() || nonEmpty == 0)
                        {
                            setStatus("Blueprint copied: selection was empty (no plans/built).", 3.0f);
                        }
                        else if (blueprint.w != oldW || blueprint.h != oldH)
                        {
                            setStatus("Blueprint copied+trimmed: " + std::to_string(oldW) + "x" + std::to_string(oldH) +
                                      " -> " + std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h) +
                                      " (" + std::to_string(nonEmpty) + " non-empty)");
                        }
                        else
                        {
                            setStatus("Blueprint copied: " + std::to_string(oldW) + "x" + std::to_string(oldH) + " (" +
                                      std::to_string(nonEmpty) + " non-empty)");
                        }
                    }
                    else
                    {
                        setStatus("Blueprint copied: " + std::to_string(bw) + "x" + std::to_string(bh) + " (" +
                                  std::to_string(nonEmpty) + " non-empty)");
                    }
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

            if (ImGui::Button("Trim Empty Borders"))
            {
                const int oldW = blueprint.w;
                const int oldH = blueprint.h;
                blueprint = colony::game::editor::BlueprintTrimEmptyBorders(blueprint);

                if (blueprint.Empty())
                {
                    setStatus("Blueprint trimmed: empty", 3.0f);
                }
                else if (blueprint.w != oldW || blueprint.h != oldH)
                {
                    setStatus("Blueprint trimmed: " + std::to_string(oldW) + "x" + std::to_string(oldH) + " -> " +
                              std::to_string(blueprint.w) + "x" + std::to_string(blueprint.h));
                }
                else
                {
                    setStatus("Blueprint trimmed: no change");
                }
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

                            const bool inSel   = isColonistInSelection(c.id);
                            const bool primary = (c.id == selectedColonistId);

                            const float r = primary ? 3.2f : (inSel ? 2.7f : 2.2f);
                            const ImU32 col = inSel ? IM_COL32(255, 240, 120, 240) : IM_COL32(235, 235, 245, 220);

                            dl->AddCircleFilled(mp, r, col);

                            if (primary)
                                dl->AddCircle(mp, r + 1.0f, IM_COL32(40, 40, 40, 180), 0, 1.5f);
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

                    // Selected room bounds
                    if (selectedRoomId >= 0)
                    {
                        const proto::World::RoomInfo* ri = world.roomInfoById(selectedRoomId);
                        if (ri)
                        {
                            const float u0 = static_cast<float>(ri->minX) / static_cast<float>(worldW);
                            const float v0 = static_cast<float>(ri->minY) / static_cast<float>(worldH);
                            const float u1 = static_cast<float>(ri->maxX + 1) / static_cast<float>(worldW);
                            const float v1 = static_cast<float>(ri->maxY + 1) / static_cast<float>(worldH);

                            dl->AddRect({p0.x + u0 * mapW, p0.y + v0 * mapH},
                                        {p0.x + u1 * mapW, p0.y + v1 * mapH},
                                        IM_COL32(180, 220, 255, 200), 0.f, 0, 2.f);
                        }
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

        ImGui::SeparatorText("Rooms");

        ImGui::Checkbox("Show rooms overlay", &showRoomsOverlay);
        ImGui::SameLine();
        ImGui::Checkbox("Indoors only##rooms_overlay", &roomsOverlayIndoorsOnly);

        ImGui::Checkbox("Show room IDs", &showRoomIds);
        ImGui::SameLine();
        ImGui::Checkbox("Indoors only##room_ids", &showRoomIdsIndoorsOnly);

        ImGui::Checkbox("Outline selected room", &showSelectedRoomOutline);

        ImGui::Text("Indoors: %d rooms, %d tiles", world.indoorsRoomCount(), world.indoorsTileCount());

        const bool canPickTileRoom = (selectedX >= 0 && selectedY >= 0 && world.inBounds(selectedX, selectedY));
        if (!canPickTileRoom)
            ImGui::BeginDisabled();

        if (ImGui::Button("Select room from selected tile"))
            selectedRoomId = world.roomIdAt(selectedX, selectedY);

        if (!canPickTileRoom)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear room selection"))
            selectedRoomId = -1;

        if (selectedRoomId >= 0)
        {
            const proto::World::RoomInfo* ri = world.roomInfoById(selectedRoomId);
            if (ri)
            {
        ImGui::Text("Selected R%d: %s", ri->id, ri->indoors ? "indoors" : "outdoors");
        ImGui::Text("Area: %d | Perim: %d | Doors: %d", ri->area, ri->perimeter, ri->doorCount);
        ImGui::Text("Bounds: (%d,%d) - (%d,%d)", ri->minX, ri->minY, ri->maxX, ri->maxY);

        if (ImGui::SmallButton("Focus camera on selected room"))
        {
            const auto& cam = camera.State();
            const float cx = (static_cast<float>(ri->minX + ri->maxX) + 1.0f) * 0.5f;
            const float cy = (static_cast<float>(ri->minY + ri->maxY) + 1.0f) * 0.5f;
            camera.ApplyPan(cx - cam.panX, cy - cam.panY);
        }
            }
            else
            {
        ImGui::TextDisabled("Selected room id is invalid (no room at that id).");
            }
        }
        else
        {
            ImGui::TextDisabled("Tip: Alt+click a tile in Inspect to select its room.");
        }

        if (ImGui::CollapsingHeader("Room Inspector", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static bool showIndoors = true;
            static bool showOutdoors = true;

            ImGui::Checkbox("Indoors##rooms_filter_in", &showIndoors);
            ImGui::SameLine();
            ImGui::Checkbox("Outdoors##rooms_filter_out", &showOutdoors);

            if (!showIndoors && !showOutdoors)
            {
        ImGui::TextDisabled("Enable at least one filter to show rooms.");
            }
            else if (ImGui::BeginTable("rooms_table", 7,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                               ImVec2(0.f, 220.f)))
            {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 46.f);
        ImGui::TableSetupColumn("In", ImGuiTableColumnFlags_WidthFixed, 30.f);
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthFixed, 52.f);
        ImGui::TableSetupColumn("Perim", ImGuiTableColumnFlags_WidthFixed, 58.f);
        ImGui::TableSetupColumn("Doors", ImGuiTableColumnFlags_WidthFixed, 54.f);
        ImGui::TableSetupColumn("Bounds", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Go", ImGuiTableColumnFlags_WidthFixed, 34.f);
        ImGui::TableHeadersRow();

        for (int rid = 0; rid < world.roomCount(); ++rid)
        {
            const proto::World::RoomInfo* ri = world.roomInfoById(rid);
            if (!ri)
                continue;

            if (ri->indoors && !showIndoors)
                continue;
            if (!ri->indoors && !showOutdoors)
                continue;

            ImGui::TableNextRow();
            ImGui::PushID(rid);

            ImGui::TableSetColumnIndex(0);
            const bool isSel = (selectedRoomId == rid);
            char idBuf[16] = {};
            (void)std::snprintf(idBuf, sizeof(idBuf), "R%d", rid);

            if (ImGui::Selectable(idBuf, isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
                selectedRoomId = rid;

            ImGui::SetItemAllowOverlap();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ri->indoors ? "Y" : "N");

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", ri->area);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", ri->perimeter);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", ri->doorCount);

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("(%d,%d)-(%d,%d)", ri->minX, ri->minY, ri->maxX, ri->maxY);

            ImGui::TableSetColumnIndex(6);
            if (ImGui::SmallButton("Go"))
            {
                const auto& cam = camera.State();
                const float cx = (static_cast<float>(ri->minX + ri->maxX) + 1.0f) * 0.5f;
                const float cy = (static_cast<float>(ri->minY + ri->maxY) + 1.0f) * 0.5f;
                camera.ApplyPan(cx - cam.panX, cy - cam.panY);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
            }
        }
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

                ImGui::SeparatorText("Build assignment");
                const unsigned long long fieldBuilds   = static_cast<unsigned long long>(stats.buildFieldComputed);
                const unsigned long long fieldSources  = static_cast<unsigned long long>(stats.buildFieldSources);
                const unsigned long long fieldAssigned = static_cast<unsigned long long>(stats.buildFieldAssigned);
                const unsigned long long fieldFallback = static_cast<unsigned long long>(stats.buildFieldFallback);
                ImGui::Text("Plan distance-field builds: %llu (sources %llu)", fieldBuilds, fieldSources);
                ImGui::Text("Assigned via field: %llu (fallback: %llu)", fieldAssigned, fieldFallback);

                ImGui::SeparatorText("Hauling assignment");
                const unsigned long long spBuilds   = static_cast<unsigned long long>(stats.haulStockpileFieldComputed);
                const unsigned long long spSources  = static_cast<unsigned long long>(stats.haulStockpileFieldSources);
                const unsigned long long spUsed     = static_cast<unsigned long long>(stats.haulStockpileFieldUsed);
                const unsigned long long hwBuilds   = static_cast<unsigned long long>(stats.haulPickupFieldComputed);
                const unsigned long long hwSources  = static_cast<unsigned long long>(stats.haulPickupFieldSources);
                const unsigned long long hwAssigned = static_cast<unsigned long long>(stats.haulPickupFieldAssigned);
                const unsigned long long hwFallback = static_cast<unsigned long long>(stats.haulPickupFieldFallback);

                ImGui::Text("Stockpile field: %llu builds (sources %llu), used %llu", spBuilds, spSources, spUsed);
                ImGui::Text("Pickup field:    %llu builds (sources %llu)", hwBuilds, hwSources);
                ImGui::Text("Assigned via field: %llu (fallback: %llu)", hwAssigned, hwFallback);

                ImGui::SeparatorText("Harvest assignment");
                const unsigned long long hfBuilds   = static_cast<unsigned long long>(stats.harvestFieldComputed);
                const unsigned long long hfSources  = static_cast<unsigned long long>(stats.harvestFieldSources);
                const unsigned long long hfAssigned = static_cast<unsigned long long>(stats.harvestFieldAssigned);
                const unsigned long long hfFallback = static_cast<unsigned long long>(stats.harvestFieldFallback);
                ImGui::Text("Harvest field builds: %llu (sources %llu)", hfBuilds, hfSources);
                ImGui::Text("Assigned via field: %llu (fallback: %llu)", hfAssigned, hfFallback);

                ImGui::SeparatorText("Eat assignment");
                const unsigned long long efBuilds   = static_cast<unsigned long long>(stats.eatFieldComputed);
                const unsigned long long efSources  = static_cast<unsigned long long>(stats.eatFieldSources);
                const unsigned long long efAssigned = static_cast<unsigned long long>(stats.eatFieldAssigned);
                const unsigned long long efFallback = static_cast<unsigned long long>(stats.eatFieldFallback);
                ImGui::Text("Food field builds: %llu (sources %llu)", efBuilds, efSources);
                ImGui::Text("Assigned via field: %llu (fallback: %llu)", efAssigned, efFallback);

                ImGui::SeparatorText("Sleep assignment");
                const unsigned long long sfBuilds   = static_cast<unsigned long long>(stats.sleepFieldComputed);
                const unsigned long long sfSources  = static_cast<unsigned long long>(stats.sleepFieldSources);
                const unsigned long long sfAssigned = static_cast<unsigned long long>(stats.sleepFieldAssigned);
                const unsigned long long sfFallback = static_cast<unsigned long long>(stats.sleepFieldFallback);
                ImGui::Text("Bed field builds: %llu (sources %llu)", sfBuilds, sfSources);
                ImGui::Text("Assigned via field: %llu (fallback: %llu)", sfAssigned, sfFallback);
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
