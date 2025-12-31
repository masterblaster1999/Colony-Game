#include "game/PrototypeGame_Impl.h"

#include "input/InputBindingParse.h"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#include "platform/win/PathUtilWin.h"
#endif

namespace colony::game {

namespace {

constexpr int kMaxBindingParents = 5;

std::vector<fs::path> CollectBindingPaths()
{
    std::vector<fs::path> out;

    // Prefer a per-user override under %LOCALAPPDATA%\\ColonyGame.
    // This avoids requiring write access to the install directory to customize bindings.
#if defined(_WIN32)
    const fs::path userDir = winpath::config_dir();
    if (!userDir.empty())
    {
        out.push_back(userDir / "input_bindings.json");
        out.push_back(userDir / "input_bindings.ini");
    }
#endif


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
    case Tool::Door: return proto::TileType::Door;
    case Tool::Farm: return proto::TileType::Farm;
    case Tool::Stockpile: return proto::TileType::Stockpile;
    case Tool::Demolish: return proto::TileType::Remove;
    case Tool::Erase: return proto::TileType::Empty;
    case Tool::Inspect: return proto::TileType::Empty;
    case Tool::Priority: return proto::TileType::Empty;
    case Tool::Blueprint: return proto::TileType::Empty;
    }
    return proto::TileType::Empty;
}

const char* PrototypeGame::Impl::toolName() const noexcept
{
    switch (tool) {
    case Tool::Inspect: return "Inspect";
    case Tool::Floor: return "Plan Floor";
    case Tool::Wall: return "Plan Wall";
    case Tool::Door: return "Plan Door";
    case Tool::Farm: return "Plan Farm";
    case Tool::Stockpile: return "Plan Stockpile";
    case Tool::Demolish: return "Demolish";
    case Tool::Erase: return "Erase Plan";
    case Tool::Priority: return "Paint Priority";
    case Tool::Blueprint: return "Blueprint Paste";
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
        bindingsLoadedPath.clear();
        DebugTraceA("[Colony] Input bindings: using defaults (no valid bindings file found)");
        DebugTraceA("[Colony] Searched candidate paths:");
        for (const auto& p : allPaths) {
            DebugTraceA(("  - " + p.string()).c_str());
        }

        setStatus("Bindings: using defaults", 4.f);
        return false;
    }

    DebugTraceA(("[Colony] Input bindings loaded: " + loaded.string()).c_str());
    bindingsLoadedPath = loaded;
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

#if defined(COLONY_WITH_IMGUI)
    // When the bindings editor is in "capture" mode we want to record raw input
    // even while ImGui is actively capturing keyboard/mouse (so action hotkeys
    // don't fire while rebinding).
    if (showBindingsEditor && bindingsEditorCaptureActive)
    {
        namespace bp = colony::input::bindings;
        using colony::input::InputEventType;

        auto cancelCapture = [&](std::string msg)
        {
            bindingsEditorCaptureActive = false;
            bindingsEditorCaptureAction = -1;
            bindingsEditorCaptureDown.reset();
            bindingsEditorCaptureCodes.clear();

            if (!msg.empty())
            {
                bindingsEditorMessage = std::move(msg);
                bindingsEditorMessageTtl = 3.f;
            }
        };

        auto commitCapture = [&](std::span<const std::uint32_t> codes)
        {
            if (bindingsEditorCaptureAction < 0 ||
                bindingsEditorCaptureAction >= static_cast<int>(colony::input::Action::Count))
            {
                cancelCapture("Capture failed: invalid action index");
                return;
            }

            // Canonicalize.
            std::vector<std::uint32_t> tmp(codes.begin(), codes.end());
            std::sort(tmp.begin(), tmp.end());
            tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());

            // InputMapper supports chords up to 4 buttons.
            if (tmp.empty())
            {
                cancelCapture("Capture failed: empty chord");
                return;
            }
            if (tmp.size() > 4)
            {
                cancelCapture("Capture failed: chord too large (max 4 inputs)");
                return;
            }

            // Convert to a user-facing chord string.
            std::string chord;
            for (std::size_t i = 0; i < tmp.size(); ++i)
            {
                if (i != 0)
                    chord.push_back('+');
                chord += bp::InputCodeToToken(tmp[i]);
            }

            std::string& field = bindingsEditorText[static_cast<std::size_t>(bindingsEditorCaptureAction)];
            if (bindingsEditorCaptureAppend && !bp::Trim(field).empty())
                field = field + ", " + chord;
            else
                field = chord;

            cancelCapture(std::string("Captured: ") + chord);
            setStatus("Bindings: captured", 2.f);
        };

        for (const auto& ev : events)
        {
            if (!bindingsEditorCaptureActive)
                break;

            switch (ev.type)
            {
            case InputEventType::FocusLost:
                cancelCapture("Capture canceled: focus lost");
                break;

            case InputEventType::KeyDown:
            {
                if (ev.repeat)
                    break;

                // ESC cancels capture (bind Esc by typing "Esc" into the field).
                if (ev.key == bp::kVK_ESCAPE)
                {
                    cancelCapture("Capture canceled");
                    break;
                }

                if (ev.key < colony::input::kInputCodeCount)
                {
                    bindingsEditorCaptureDown.set(ev.key);
                    // Record the key as part of the chord.
                    const auto it = std::find(bindingsEditorCaptureCodes.begin(), bindingsEditorCaptureCodes.end(), ev.key);
                    if (it == bindingsEditorCaptureCodes.end())
                        bindingsEditorCaptureCodes.push_back(ev.key);
                }
                break;
            }

            case InputEventType::KeyUp:
                if (ev.key < colony::input::kInputCodeCount)
                    bindingsEditorCaptureDown.reset(ev.key);
                break;

            case InputEventType::MouseButtonDown:
                if (ev.key < colony::input::kInputCodeCount)
                {
                    bindingsEditorCaptureDown.set(ev.key);
                    const auto it = std::find(bindingsEditorCaptureCodes.begin(), bindingsEditorCaptureCodes.end(), ev.key);
                    if (it == bindingsEditorCaptureCodes.end())
                        bindingsEditorCaptureCodes.push_back(ev.key);
                }
                break;

            case InputEventType::MouseButtonUp:
                if (ev.key < colony::input::kInputCodeCount)
                    bindingsEditorCaptureDown.reset(ev.key);
                break;

            case InputEventType::MouseWheel:
            {
                // Wheel is an impulse; finalize immediately after adding.
                const std::uint32_t wheelCode = (ev.wheelDetents > 0) ? colony::input::kMouseWheelUp : colony::input::kMouseWheelDown;
                const auto it = std::find(bindingsEditorCaptureCodes.begin(), bindingsEditorCaptureCodes.end(), wheelCode);
                if (it == bindingsEditorCaptureCodes.end())
                    bindingsEditorCaptureCodes.push_back(wheelCode);
                break;
            }

            default:
                break;
            }
        }

        // Finalize a capture once all pressed keys/buttons have been released.
        if (bindingsEditorCaptureActive && !bindingsEditorCaptureCodes.empty() && bindingsEditorCaptureDown.none())
        {
            commitCapture(std::span<const std::uint32_t>(bindingsEditorCaptureCodes.data(), bindingsEditorCaptureCodes.size()));
        }
    }
#endif

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
            case '7': tool = Tool::Priority; changed = true; break;
            case '8': tool = Tool::Demolish; changed = true; break;
            case '9': tool = Tool::Blueprint; changed = true; break;

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
    // Respect ImGui capture so Ctrl+S/Ctrl+L (and similar chords) don't fire while typing in UI widgets.
    if (!uiWantsKeyboard)
    {
        for (const auto& actionEvent : input.ActionEvents())
        {
            if (actionEvent.type != colony::input::ActionEventType::Pressed)
                continue;

            switch (actionEvent.action)
            {
            case colony::input::Action::ReloadBindings:
                (void)loadBindings();
                changed = true;
                break;

            case colony::input::Action::SaveWorld:
                (void)saveWorld();
                changed = true;
                break;

            case colony::input::Action::LoadWorld:
                (void)loadWorld();
                changed = true;
                break;

            case colony::input::Action::Undo:
                if (undoPlans())
                    changed = true;
                break;

            case colony::input::Action::Redo:
                if (redoPlans())
                    changed = true;
                break;

            case colony::input::Action::PlanPriorityUp:
            {
                const int old = planBrushPriority;
                planBrushPriority = std::min(3, planBrushPriority + 1);
                if (planBrushPriority != old)
                {
                    setStatus("Brush priority: " + std::to_string(planBrushPriority + 1), 1.25f);
                    changed = true;
                }
                break;
            }

            case colony::input::Action::PlanPriorityDown:
            {
                const int old = planBrushPriority;
                planBrushPriority = std::max(0, planBrushPriority - 1);
                if (planBrushPriority != old)
                {
                    setStatus("Brush priority: " + std::to_string(planBrushPriority + 1), 1.25f);
                    changed = true;
                }
                break;
            }

            default:
                break;
            }
        }
    }

    return changed;
}

} // namespace colony::game
