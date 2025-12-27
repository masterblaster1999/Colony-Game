#include "input/InputMapper.h"

#include "util/TextEncoding.h"

#if defined(_WIN32)
#include "platform/win/PathUtilWin.h"
#endif

#include "input/InputBindingParse.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

static bool ReadFileToString(const std::filesystem::path& path, std::string& out) noexcept
{
    out.clear();

#if defined(_WIN32)
    // Input bindings are user-editable and may be briefly locked by editors or scanners while saving.
    // Use retry/backoff reads to avoid spurious fallbacks to default bindings during hot-reload.
    constexpr std::size_t kMaxBindingsBytes = 4u * 1024u * 1024u; // 4 MiB guardrail

    std::error_code ec;
    if (!winpath::read_file_to_string_with_retry(path, out, &ec, kMaxBindingsBytes, /*max_attempts=*/32))
        return false;

    // Treat empty files as invalid.
    return !out.empty();
#else
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz <= 0)
        return false;

    out.resize(static_cast<std::size_t>(sz));
    f.seekg(0, std::ios::beg);
    f.read(out.data(), static_cast<std::streamsize>(sz));
    // Ensure the full file was read; this avoids subtle partial-read failures
    // (AV scanners/locking) being treated as successful loads.
    return (f.gcount() == static_cast<std::streamsize>(sz));
#endif
}

static std::optional<colony::input::Action> ParseActionName(std::string_view name)
{
    const std::string n = colony::input::bindings::ToLowerCopy(colony::input::bindings::Trim(name));

    using A = colony::input::Action;

    if (n == "moveforward" || n == "forward" || n == "w") return A::MoveForward;
    if (n == "movebackward" || n == "backward" || n == "s") return A::MoveBackward;
    if (n == "moveleft" || n == "left" || n == "a") return A::MoveLeft;
    if (n == "moveright" || n == "right" || n == "d") return A::MoveRight;
    if (n == "movedown" || n == "down" || n == "q") return A::MoveDown;
    if (n == "moveup" || n == "up" || n == "e") return A::MoveUp;

    if (n == "moveforwardfast" || n == "forwardfast" || n == "fastforward" || n == "shift+w") return A::MoveForwardFast;

    if (n == "speedboost" || n == "boost" || n == "shift") return A::SpeedBoost;

    if (n == "cameraorbit" || n == "orbit") return A::CameraOrbit;
    if (n == "camerapan" || n == "pan") return A::CameraPan;

    if (n == "camerazoomin" || n == "zoomin" || n == "zoom_in" || n == "zoom+" || n == "wheelup") return A::CameraZoomIn;
    if (n == "camerazoomout" || n == "zoomout" || n == "zoom_out" || n == "zoom-" || n == "wheeldown") return A::CameraZoomOut;

    // Developer QOL
    if (n == "reloadbindings" || n == "reloadbinds" || n == "reloadinputs" || n == "reload") return A::ReloadBindings;

    // Prototype persistence
    if (n == "saveworld" || n == "save" || n == "savegame" || n == "save_proto") return A::SaveWorld;
    if (n == "loadworld" || n == "load" || n == "loadgame" || n == "load_proto") return A::LoadWorld;

    // Prototype editor QOL
    if (n == "undo" || n == "undoplans" || n == "undo_plan" || n == "undo_plans" || n == "ctrl+z") return A::Undo;
    if (n == "redo" || n == "redoplans" || n == "redo_plan" || n == "redo_plans" || n == "ctrl+y" || n == "ctrl+shift+z") return A::Redo;

    // Prototype build-planning QOL
    if (n == "planpriorityup" || n == "priorityup" || n == "increasepriority") return A::PlanPriorityUp;
    if (n == "planprioritydown" || n == "prioritydown" || n == "decreasepriority") return A::PlanPriorityDown;

    return std::nullopt;
}

static bool SplitOnce(std::string_view s, char delim, std::string_view& left, std::string_view& right) noexcept
{
    const std::size_t pos = s.find(delim);
    if (pos == std::string_view::npos)
        return false;

    left = s.substr(0, pos);
    right = s.substr(pos + 1);
    return true;
}

} // namespace

namespace colony::input {

InputMapper::InputMapper() noexcept
{
    SetDefaultBinds();
    ClearState();
}

void InputMapper::SetDefaultBinds() noexcept
{
    namespace bindings = colony::input::bindings;

    // Clear all binds.
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        m_bindCounts[i] = 0;
        for (auto& c : m_binds[i]) {
            c.count = 0;
            c.codes.fill(0);
        }
    }

    // Classic free-cam movement defaults + arrow key alternatives.
    AddBinding(Action::MoveForward,  static_cast<std::uint32_t>('W'));
    AddBinding(Action::MoveForward,  bindings::kVK_UP);

    AddBinding(Action::MoveBackward, static_cast<std::uint32_t>('S'));
    AddBinding(Action::MoveBackward, bindings::kVK_DOWN);

    AddBinding(Action::MoveLeft,     static_cast<std::uint32_t>('A'));
    AddBinding(Action::MoveLeft,     bindings::kVK_LEFT);

    AddBinding(Action::MoveRight,    static_cast<std::uint32_t>('D'));
    AddBinding(Action::MoveRight,    bindings::kVK_RIGHT);

    AddBinding(Action::MoveDown,     static_cast<std::uint32_t>('Q'));
    AddBinding(Action::MoveUp,       static_cast<std::uint32_t>('E'));

    // Example chord binding: Shift+W as a distinct action.
    {
        const std::uint32_t chord[] = { bindings::kVK_SHIFT, static_cast<std::uint32_t>('W') };
        AddBinding(Action::MoveForwardFast, std::span<const std::uint32_t>(chord, 2));
    }

    // Speed boost modifier (either shift).
    AddBinding(Action::SpeedBoost, bindings::kVK_SHIFT);
    AddBinding(Action::SpeedBoost, bindings::kVK_LSHIFT);
    AddBinding(Action::SpeedBoost, bindings::kVK_RSHIFT);

    // Mouse-driven camera actions.
    AddBinding(Action::CameraOrbit, kMouseButtonLeft);
    AddBinding(Action::CameraPan, kMouseButtonMiddle);
    AddBinding(Action::CameraPan, kMouseButtonRight);

    // Mouse wheel zoom.
    AddBinding(Action::CameraZoomIn, kMouseWheelUp);
    AddBinding(Action::CameraZoomOut, kMouseWheelDown);

    // Optional chord example: Shift+MouseLeft => pan.
    {
        const std::uint32_t chord[] = { bindings::kVK_SHIFT, kMouseButtonLeft };
        AddBinding(Action::CameraPan, std::span<const std::uint32_t>(chord, 2));
    }

    // Hot reload input bindings (defaults to F5).
    AddBinding(Action::ReloadBindings, bindings::VK_F(5));

    // Prototype persistence: quick save/load of the proto world.
    // NOTE: F6/F7 are reserved for window-level hotkeys (FPS caps / unfocused behavior), so defaults use Ctrl+S / Ctrl+L.
    // NOTE: Generic Ctrl/Shift/Alt modifiers are supported by the mapper (either L/R works).
    {
        const std::uint32_t chordSave[] = { bindings::kVK_CONTROL, static_cast<std::uint32_t>('S') };
        AddBinding(Action::SaveWorld, std::span<const std::uint32_t>(chordSave, 2));
    }
    {
        const std::uint32_t chordLoad[] = { bindings::kVK_CONTROL, static_cast<std::uint32_t>('L') };
        AddBinding(Action::LoadWorld, std::span<const std::uint32_t>(chordLoad, 2));
    }

    // Prototype editor QOL: undo/redo plan placement.
    {
        const std::uint32_t chordUndo[] = { bindings::kVK_CONTROL, static_cast<std::uint32_t>('Z') };
        AddBinding(Action::Undo, std::span<const std::uint32_t>(chordUndo, 2));
    }
    {
        const std::uint32_t chordRedo[] = { bindings::kVK_CONTROL, static_cast<std::uint32_t>('Y') };
        AddBinding(Action::Redo, std::span<const std::uint32_t>(chordRedo, 2));

        // Common alternative on some editors: Ctrl+Shift+Z
        const std::uint32_t chordRedo2[] = { bindings::kVK_CONTROL, bindings::kVK_SHIFT, static_cast<std::uint32_t>('Z') };
        AddBinding(Action::Redo, std::span<const std::uint32_t>(chordRedo2, 3));
    }

    // Build planning QOL: plan priority up/down (defaults to PgUp/PgDn).
    AddBinding(Action::PlanPriorityUp, bindings::kVK_PRIOR);
    AddBinding(Action::PlanPriorityDown, bindings::kVK_NEXT);
    RecomputeActionStatesNoEvents();
}

bool InputMapper::LoadFromFile(const std::filesystem::path& path) noexcept
{
    std::string text;
    if (!ReadFileToString(path, text))
        return false;

    if (!colony::util::NormalizeTextToUtf8(text))
        return false;

    const auto ext = colony::input::bindings::ToLowerCopy(path.extension().string());

    bool ok = false;
    if (ext == ".json")
        ok = LoadFromJsonText(text);
    else if (ext == ".ini")
        ok = LoadFromIniText(text);
    else
    {
        // Best-effort format sniff.
        const std::string_view sv = text;
        std::size_t i = 0;
        while (i < sv.size() && colony::input::bindings::IsWhitespace(sv[i])) ++i;
        if (i < sv.size() && sv[i] == '{')
            ok = LoadFromJsonText(text);
        else
            ok = LoadFromIniText(text);
    }

    if (ok)
    {
        // Bind sets changed; refresh cached down-state without producing events.
        RecomputeActionStatesNoEvents();
    }

    return ok;
}

bool InputMapper::LoadFromDefaultPaths() noexcept
{
    // Walk up a few parent directories starting from the current working dir.
    // This makes it resilient to running from e.g. build/bin/Debug.
    constexpr int kMaxParents = 5;

    std::error_code ec;
    std::filesystem::path base = std::filesystem::current_path(ec);
    if (ec || base.empty())
        base = std::filesystem::path(".");

#if defined(_WIN32)
    // Prefer a per-user override under %LOCALAPPDATA%\ColonyGame. This avoids
    // requiring write access to the install directory to customize bindings.
    {
        const std::filesystem::path userDir = winpath::config_dir();
        if (!userDir.empty())
        {
            const std::filesystem::path userCandidates[] = {
                userDir / "input_bindings.json",
                userDir / "input_bindings.ini",
            };

            for (const auto& c : userCandidates)
            {
                ec.clear();
                if (std::filesystem::exists(c, ec) && !ec)
                {
                    if (LoadFromFile(c))
                        return true;
                }
            }
        }
    }
#endif

    for (int depth = 0; depth <= kMaxParents; ++depth)
    {
        const std::filesystem::path candidates[] = {
            base / "assets" / "config" / "input_bindings.json",
            base / "assets" / "config" / "input_bindings.ini",
            base / "input_bindings.json",
            base / "input_bindings.ini",
        };

        for (const auto& c : candidates)
        {
            ec.clear();
            if (std::filesystem::exists(c, ec) && !ec)
            {
                if (LoadFromFile(c))
                    return true;
            }
        }

        if (!base.has_parent_path())
            break;
        const auto parent = base.parent_path();
        if (parent == base)
            break;
        base = parent;
    }

    return false;
}


void InputMapper::ClearState() noexcept
{
    m_down.reset();
    m_actionDown.fill(false);
    m_actionEventCount = 0;
}

void InputMapper::ClearBindings(Action action) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return;

    m_bindCounts[idx] = 0;
    for (auto& c : m_binds[idx]) {
        c.count = 0;
        c.codes.fill(0);
    }

    RecomputeActionStatesNoEvents();
}

void InputMapper::AddBinding(Action action, std::span<const std::uint32_t> chord) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return;

    if (chord.empty())
        return;

    // Build a canonical chord: sorted, de-duplicated, clamped.
    std::array<std::uint32_t, kMaxChordButtons> tmp{};
    std::size_t tmpCount = 0;

    for (const std::uint32_t code : chord)
    {
        if (code >= kMaxInputCodes)
            continue;

        bool dup = false;
        for (std::size_t i = 0; i < tmpCount; ++i)
        {
            if (tmp[i] == code) { dup = true; break; }
        }
        if (dup)
            continue;

        if (tmpCount < kMaxChordButtons)
            tmp[tmpCount++] = code;
        else
            return; // chord too large
    }

    if (tmpCount == 0)
        return;

    std::sort(tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(tmpCount));

    // Reject duplicates.
    const auto count = static_cast<std::size_t>(m_bindCounts[idx]);
    for (std::size_t b = 0; b < count; ++b)
    {
        const Chord& existing = m_binds[idx][b];
        if (existing.count != tmpCount)
            continue;

        bool same = true;
        for (std::size_t i = 0; i < tmpCount; ++i)
        {
            if (existing.codes[i] != static_cast<std::uint16_t>(tmp[i])) { same = false; break; }
        }
        if (same)
            return;
    }

    if (count >= kMaxBindingsPerAction)
        return;

    Chord c{};
    c.count = static_cast<std::uint8_t>(tmpCount);
    for (std::size_t i = 0; i < tmpCount; ++i)
        c.codes[i] = static_cast<std::uint16_t>(tmp[i]);

    m_binds[idx][count] = c;
    m_bindCounts[idx] = static_cast<std::uint8_t>(count + 1);

    RecomputeActionStatesNoEvents();
}

void InputMapper::BeginFrame() noexcept
{
    m_actionEventCount = 0;
}

std::span<const ActionEvent> InputMapper::ActionEvents() const noexcept
{
    return std::span<const ActionEvent>(m_actionEvents.data(), m_actionEventCount);
}

void InputMapper::PushActionEvent(Action action, ActionEventType type) noexcept
{
    if (m_actionEventCount < kMaxActionEvents)
    {
        m_actionEvents[m_actionEventCount++] = ActionEvent{ action, type };
    }
    else
    {
        ++m_droppedActionEvents;
    }
}

bool InputMapper::ComputeActionDown(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return false;

    const auto bindCount = static_cast<std::size_t>(m_bindCounts[idx]);
    for (std::size_t b = 0; b < bindCount; ++b)
    {
        const Chord& c = m_binds[idx][b];
        if (c.count == 0)
            continue;

        bool allDown = true;
        for (std::size_t i = 0; i < c.count; ++i)
        {
            const auto code = static_cast<std::size_t>(c.codes[i]);
            if (code >= kMaxInputCodes || !m_down.test(code))
            {
                allDown = false;
                break;
            }
        }

        if (allDown)
            return true;
    }

    return false;
}

void InputMapper::RecomputeActionStatesNoEvents() noexcept
{
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        const auto a = static_cast<Action>(i);
        m_actionDown[i] = ComputeActionDown(a);
    }
}

void InputMapper::RefreshActionsAndEmitTransitions() noexcept
{
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        const auto a = static_cast<Action>(i);
        const bool newDown = ComputeActionDown(a);
        if (newDown != m_actionDown[i])
        {
            PushActionEvent(a, newDown ? ActionEventType::Pressed : ActionEventType::Released);
            m_actionDown[i] = newDown;
        }
    }
}

bool InputMapper::ConsumeEvent(const InputEvent& ev) noexcept
{
    const std::size_t before = m_actionEventCount;

    bool recompute = false;

    // Normalize generic modifiers (Shift/Ctrl/Alt) so bindings like "Ctrl+S" work
    // regardless of whether the OS reports left/right variants.
    //
    // Windows commonly reports VK_LSHIFT/VK_RSHIFT (and similar for Ctrl/Alt),
    // while human-friendly bindings often use VK_SHIFT/VK_CONTROL/VK_MENU.
    auto syncGenericModifiers = [&]() noexcept
    {
        namespace bindings = colony::input::bindings;

        auto sync = [&](std::uint32_t generic, std::uint32_t left, std::uint32_t right) noexcept
        {
            const std::size_t g = static_cast<std::size_t>(generic);
            if (g >= kMaxInputCodes)
                return;

            const std::size_t l = static_cast<std::size_t>(left);
            const std::size_t r = static_cast<std::size_t>(right);

            const bool any =
                (l < kMaxInputCodes && m_down.test(l)) ||
                (r < kMaxInputCodes && m_down.test(r));

            if (any) m_down.set(g);
            else     m_down.reset(g);
        };

        sync(bindings::kVK_SHIFT,   bindings::kVK_LSHIFT,   bindings::kVK_RSHIFT);
        sync(bindings::kVK_CONTROL, bindings::kVK_LCONTROL, bindings::kVK_RCONTROL);
        sync(bindings::kVK_MENU,    bindings::kVK_LMENU,    bindings::kVK_RMENU);
    };

    switch (ev.type)
    {
    case InputEventType::KeyDown:
    case InputEventType::MouseButtonDown:
    {
        const auto code = static_cast<std::size_t>(ev.key);
        if (code < kMaxInputCodes)
        {
            const bool wasDown = m_down.test(code);
            m_down.set(code);

            // Keep generic modifiers in sync (VK_SHIFT/VK_CONTROL/VK_MENU).
            syncGenericModifiers();

            // Ignore repeats; but still keep state sane.
            recompute = (!wasDown);
        }
        break;
    }

    case InputEventType::KeyUp:
    case InputEventType::MouseButtonUp:
    {
        const auto code = static_cast<std::size_t>(ev.key);
        if (code < kMaxInputCodes)
        {
            const bool wasDown = m_down.test(code);
            m_down.reset(code);

            // Keep generic modifiers in sync (VK_SHIFT/VK_CONTROL/VK_MENU).
            syncGenericModifiers();

            recompute = wasDown;
        }
        break;
    }

    case InputEventType::FocusLost:
        // Key/button up may never be delivered once focus is gone; clear everything and
        // emit releases for any active actions.
        m_down.reset();
        syncGenericModifiers();
        recompute = true;
        break;

    case InputEventType::MouseWheel:
    {
        const int detents = static_cast<int>(ev.wheelDetents);
        if (detents == 0)
            break;

        const std::size_t code = static_cast<std::size_t>(detents > 0 ? kMouseWheelUp : kMouseWheelDown);
        if (code >= kMaxInputCodes)
            break;

        const int steps = (detents > 0) ? detents : -detents;
        for (int i = 0; i < steps; ++i)
        {
            // Wheel is an impulse; synthesize a press + release so bindings can
            // be expressed as normal chords.
            m_down.set(code);
            RefreshActionsAndEmitTransitions();

            m_down.reset(code);
            RefreshActionsAndEmitTransitions();
        }
        break;
    }

    default:
        break;
    }

    if (recompute)
        RefreshActionsAndEmitTransitions();

    return m_actionEventCount != before;
}

bool InputMapper::Consume(std::span<const InputEvent> events) noexcept
{
    BeginFrame();

    for (const auto& ev : events)
        ConsumeEvent(ev);

    return m_actionEventCount != 0;
}

bool InputMapper::IsDown(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return false;
    return m_actionDown[idx];
}

MovementAxes InputMapper::GetMovementAxes() const noexcept
{
    MovementAxes a{};

    a.x = (IsDown(Action::MoveRight) ? 1.f : 0.f) - (IsDown(Action::MoveLeft) ? 1.f : 0.f);

    float forward = (IsDown(Action::MoveForward) ? 1.f : 0.f) - (IsDown(Action::MoveBackward) ? 1.f : 0.f);
    if (IsDown(Action::MoveForwardFast))
        forward = 1.f; // chord overrides plain forward to stay stable
    a.y = forward;

    a.z = (IsDown(Action::MoveUp) ? 1.f : 0.f) - (IsDown(Action::MoveDown) ? 1.f : 0.f);

    return a;
}

std::size_t InputMapper::BindingCount(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return 0;
    return static_cast<std::size_t>(m_bindCounts[idx]);
}

std::span<const std::uint16_t> InputMapper::BindingChord(Action action, std::size_t bindingIndex) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return {};

    const auto bindCount = static_cast<std::size_t>(m_bindCounts[idx]);
    if (bindingIndex >= bindCount)
        return {};

    const Chord& c = m_binds[idx][bindingIndex];
    return std::span<const std::uint16_t>(c.codes.data(), static_cast<std::size_t>(c.count));
}

const char* InputMapper::ActionName(Action action) noexcept
{
    switch (action)
    {
    case Action::MoveForward:      return "MoveForward";
    case Action::MoveBackward:     return "MoveBackward";
    case Action::MoveLeft:         return "MoveLeft";
    case Action::MoveRight:        return "MoveRight";
    case Action::MoveDown:         return "MoveDown";
    case Action::MoveUp:           return "MoveUp";
    case Action::MoveForwardFast:  return "MoveForwardFast";
    case Action::SpeedBoost:       return "SpeedBoost";
    case Action::CameraOrbit:      return "CameraOrbit";
    case Action::CameraPan:        return "CameraPan";
    case Action::CameraZoomIn:     return "CameraZoomIn";
    case Action::CameraZoomOut:    return "CameraZoomOut";
    case Action::ReloadBindings:   return "ReloadBindings";
    case Action::SaveWorld:        return "SaveWorld";
    case Action::LoadWorld:        return "LoadWorld";
    case Action::Undo:             return "Undo";
    case Action::Redo:             return "Redo";
    case Action::PlanPriorityUp:   return "PlanPriorityUp";
    case Action::PlanPriorityDown: return "PlanPriorityDown";
    case Action::Count:            break;
    }
    return "Unknown";
}

bool InputMapper::LoadFromJsonText(std::string_view text) noexcept
{
    using json = nlohmann::json;

    json j = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (j.is_discarded())
        return false;

    // Accept either:
    //   { "bindings": { "MoveForward": ["W", "Up"], ... } }
    // or
    //   { "MoveForward": ["W", "Up"], ... }
    json* binds = &j;
    if (j.contains("bindings"))
        binds = &j["bindings"];

    if (!binds->is_object())
        return false;

    bool any = false;

    std::vector<std::uint32_t> chordCodes;
    std::vector<std::vector<std::uint32_t>> parsedChords;

    for (auto it = binds->begin(); it != binds->end(); ++it)
    {
        const auto act = ParseActionName(it.key());
        if (!act)
            continue;

        parsedChords.clear();

        const json& v = it.value();

        auto considerBindStr = [&](std::string_view bindStr)
        {
            // Allow comma-separated binds in a single string for convenience.
            for (auto part : colony::input::bindings::Split(bindStr, ','))
            {
                part = colony::input::bindings::Trim(part);
                if (part.empty())
                    continue;

                if (!colony::input::bindings::ParseChordString(part, chordCodes))
                    continue;

                parsedChords.emplace_back(chordCodes.begin(), chordCodes.end());
            }
        };

        if (v.is_string())
        {
            std::string_view s = v.get_ref<const std::string&>();
            if (colony::input::bindings::Trim(s).empty())
            {
                // Explicit clear.
                ClearBindings(*act);
                any = true;
                continue;
            }
            considerBindStr(s);
        }
        else if (v.is_array())
        {
            if (v.empty())
            {
                // Explicit clear.
                ClearBindings(*act);
                any = true;
                continue;
            }

            for (const auto& item : v)
            {
                if (!item.is_string())
                    continue;
                considerBindStr(item.get_ref<const std::string&>());
            }
        }

        // Only override existing binds if we parsed at least one valid binding.
        if (parsedChords.empty())
            continue;

        ClearBindings(*act);
        for (const auto& chord : parsedChords)
        {
            AddBinding(*act, std::span<const std::uint32_t>(chord.data(), chord.size()));
        }

        any = true;
    }

    return any;
}

bool InputMapper::LoadFromIniText(std::string_view text) noexcept
{
    bool any = false;

    std::string_view currentSection;

    std::vector<std::uint32_t> chordCodes;
    std::vector<std::vector<std::uint32_t>> parsedChords;

    std::size_t lineStart = 0;
    while (lineStart < text.size())
    {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos)
            lineEnd = text.size();

        std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        // Strip CR for Windows-style newlines.
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        line = colony::input::bindings::Trim(line);
        if (line.empty())
            continue;

        // Comments (whole-line)
        if (line.starts_with("#") || line.starts_with(";") || line.starts_with("//"))
            continue;

        // [Section]
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = colony::input::bindings::Trim(line.substr(1, line.size() - 2));
            continue;
        }

        // Only read bindings from [Bindings] section if present.
        if (!currentSection.empty())
        {
            const std::string secLower = colony::input::bindings::ToLowerCopy(currentSection);
            if (secLower != "bindings")
                continue;
        }

        std::string_view key;
        std::string_view value;
        if (!SplitOnce(line, '=', key, value) && !SplitOnce(line, ':', key, value))
            continue;

        key = colony::input::bindings::Trim(key);
        value = colony::input::bindings::Trim(value);

        // Strip trailing inline comments (simple, but good enough).
        const std::size_t hashPos = value.find('#');
        const std::size_t semiPos = value.find(';');
        std::size_t cut = std::string_view::npos;
        if (hashPos != std::string_view::npos) cut = hashPos;
        if (semiPos != std::string_view::npos) cut = (cut == std::string_view::npos) ? semiPos : std::min(cut, semiPos);
        if (cut != std::string_view::npos)
            value = colony::input::bindings::Trim(value.substr(0, cut));

        const auto act = ParseActionName(key);
        if (!act)
            continue;

        // "Action =" explicitly clears existing binds.
        if (value.empty())
        {
            ClearBindings(*act);
            any = true;
            continue;
        }

        parsedChords.clear();

        for (auto bindStr : colony::input::bindings::Split(value, ','))
        {
            bindStr = colony::input::bindings::Trim(bindStr);
            if (bindStr.empty())
                continue;

            if (!colony::input::bindings::ParseChordString(bindStr, chordCodes))
                continue;

            parsedChords.emplace_back(chordCodes.begin(), chordCodes.end());
        }

        // Only override if we parsed at least one valid binding.
        if (parsedChords.empty())
            continue;

        ClearBindings(*act);
        for (const auto& chord : parsedChords)
        {
            AddBinding(*act, std::span<const std::uint32_t>(chord.data(), chord.size()));
        }

        any = true;
    }

    return any;
}


} // namespace colony::input
