#include "game/PrototypeGame_Impl.h"

#include "input/InputBindingParse.h"

#include <system_error>

namespace colony::game {

namespace {

constexpr int kMaxBindingParents = 5;

std::vector<fs::path> CollectBindingPaths()
{
    std::vector<fs::path> out;

    std::error_code ec;
    fs::path base = fs::current_path(ec);
    if (ec || base.empty())
        base = fs::path(".");

    for (int depth = 0; depth <= kMaxBindingParents; ++depth) {
        const fs::path candidates[] = {
            base / "assets" / "config" / "input_bindings.json",
            base / "assets" / "config" / "input_bindings.ini",
            base / "input_bindings.json",
            base / "input_bindings.ini",
        };

        for (const auto& c : candidates)
            out.push_back(c);

        if (!base.has_parent_path())
            break;
        const auto parent = base.parent_path();
        if (parent == base)
            break;
        base = parent;
    }

    // De-dupe while preserving order.
    std::vector<fs::path> unique;
    unique.reserve(out.size());
    for (const auto& p : out) {
        bool seen = false;
        for (const auto& u : unique) {
            if (u == p) {
                seen = true;
                break;
            }
        }
        if (!seen)
            unique.push_back(p);
    }
    return unique;
}

} // namespace

proto::TileType PrototypeGame::Impl::toolTile() const noexcept
{
    switch (tool) {
    case Tool::Floor: return proto::TileType::Floor;
    case Tool::Wall: return proto::TileType::Wall;
    case Tool::Farm: return proto::TileType::Farm;
    case Tool::Stockpile: return proto::TileType::Stockpile;
    case Tool::Erase: return proto::TileType::Empty;
    case Tool::Inspect: return proto::TileType::Empty;
    }
    return proto::TileType::Empty;
}

const char* PrototypeGame::Impl::toolName() const noexcept
{
    switch (tool) {
    case Tool::Inspect: return "Inspect";
    case Tool::Floor: return "Plan Floor";
    case Tool::Wall: return "Plan Wall";
    case Tool::Farm: return "Plan Farm";
    case Tool::Stockpile: return "Plan Stockpile";
    case Tool::Erase: return "Erase Plan";
    }
    return "(unknown)";
}

void PrototypeGame::Impl::setStatus(std::string text, float ttlSeconds)
{
    statusText = std::move(text);
    statusTtl  = ttlSeconds;
}

bool PrototypeGame::Impl::loadBindings()
{
    using colony::appwin::win32::DebugTraceA;

    const auto allPaths = CollectBindingPaths();

    // Refresh watch list (timestamps) for any existing candidates.
    bindingCandidates.clear();
    for (const auto& p : allPaths) {
        std::error_code ec;
        if (!fs::exists(p, ec) || ec)
            continue;

        ec.clear();
        const auto wt = fs::last_write_time(p, ec);
        if (!ec)
            bindingCandidates.emplace_back(p, wt);
    }

    fs::path loaded;
    for (const auto& p : allPaths) {
        std::error_code ec;
        if (!fs::exists(p, ec) || ec)
            continue;
        if (input.LoadFromFile(p)) {
            loaded = p;
            break;
        }
    }

    if (loaded.empty()) {
        DebugTraceA("[Colony] Input bindings: using defaults (no valid bindings file found)");
        DebugTraceA("[Colony] Searched candidate paths:");
        for (const auto& p : allPaths) {
            DebugTraceA(("  - " + p.string()).c_str());
        }

        setStatus("Bindings: using defaults", 4.f);
        return false;
    }

    DebugTraceA(("[Colony] Input bindings loaded: " + loaded.string()).c_str());
    setStatus("Bindings: loaded", 1.5f);
    return true;
}

void PrototypeGame::Impl::pollBindingHotReload(float dtSeconds)
{
    if (!bindingHotReloadEnabled)
        return;

    bindingsPollAccum += dtSeconds;
    if (bindingsPollAccum < bindingsPollInterval)
        return;
    bindingsPollAccum = 0.f;

    bool changed = false;
    for (auto& [p, lastT] : bindingCandidates) {
        std::error_code ec;
        if (!fs::exists(p, ec) || ec)
            continue;

        ec.clear();
        const auto nowT = fs::last_write_time(p, ec);
        if (ec)
            continue;

        if (nowT != lastT) {
            lastT   = nowT;
            changed = true;
        }
    }

    if (changed)
        (void)loadBindings();
}

bool PrototypeGame::Impl::OnInput(std::span<const colony::input::InputEvent> events,
                                 bool uiWantsKeyboard,
                                 bool /*uiWantsMouse*/) noexcept
{
    bool changed = false;

    // Feed events into the mapper first (this also resets ActionEvents for this batch).
    (void)input.Consume(events);

    // Gameplay hotkeys (only when ImGui isn't capturing keyboard).
    if (!uiWantsKeyboard) {
        for (const auto& ev : events) {
            if (ev.type != colony::input::InputEventType::KeyDown || ev.repeat)
                continue;

            switch (ev.key) {
            case '1': tool = Tool::Inspect; changed = true; break;
            case '2': tool = Tool::Floor; changed = true; break;
            case '3': tool = Tool::Wall; changed = true; break;
            case '4': tool = Tool::Farm; changed = true; break;
            case '5': tool = Tool::Stockpile; changed = true; break;
            case '6': tool = Tool::Erase; changed = true; break;

            case 'P':
                paused = !paused;
                setStatus(paused ? "Simulation paused" : "Simulation running");
                changed = true;
                break;

            case 'R':
                resetWorld();
                changed = true;
                break;

            case colony::input::bindings::kVK_F1:
                showPanels = !showPanels;
                setStatus(showPanels ? "Panels: shown" : "Panels: hidden", 1.5f);
                changed = true;
                break;

            case colony::input::bindings::kVK_F2:
                showHelp = !showHelp;
                setStatus(showHelp ? "Help: shown" : "Help: hidden", 1.5f);
                changed = true;
                break;

            default:
                break;
            }
        }
    }

    // Discrete actions from bindings file.
    for (const auto& actionEvent : input.ActionEvents()) {
        switch (actionEvent.action) {
        case colony::input::Action::ReloadBindings:
            if (actionEvent.type == colony::input::ActionEventType::Pressed) {
                (void)loadBindings();
                changed = true;
            }
            break;
        default:
            break;
        }
    }

    return changed;
}

} // namespace colony::game
