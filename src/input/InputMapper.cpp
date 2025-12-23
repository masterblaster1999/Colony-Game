#include "input/InputMapper.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

// Win32 virtual-key codes we want for defaults/config parsing, but without including <windows.h>
// (this input layer remains platform-agnostic).
// IMPORTANT: We intentionally do *not* name these like VK_*.
// On Windows, <Windows.h> defines VK_* as macros. This translation unit is built with
// a project-wide PCH that includes <Windows.h>, and naming collisions would produce
// invalid macro-expanded declarations (e.g. "constexpr std::uint32_t 0x10 = 0x10;").
//
// Using a prefix avoids brittle #undefs and keeps this file platform-agnostic.
constexpr std::uint32_t kVK_SHIFT    = 0x10;
constexpr std::uint32_t kVK_CONTROL  = 0x11;
constexpr std::uint32_t kVK_MENU     = 0x12; // Alt

constexpr std::uint32_t kVK_LSHIFT   = 0xA0;
constexpr std::uint32_t kVK_RSHIFT   = 0xA1;
constexpr std::uint32_t kVK_LCONTROL = 0xA2;
constexpr std::uint32_t kVK_RCONTROL = 0xA3;
constexpr std::uint32_t kVK_LMENU    = 0xA4;
constexpr std::uint32_t kVK_RMENU    = 0xA5;

constexpr std::uint32_t kVK_LEFT  = 0x25;
constexpr std::uint32_t kVK_UP    = 0x26;
constexpr std::uint32_t kVK_RIGHT = 0x27;
constexpr std::uint32_t kVK_DOWN  = 0x28;

constexpr std::uint32_t kVK_SPACE = 0x20;

// Common navigation/utility keys (useful for config files / future gameplay actions).
constexpr std::uint32_t kVK_ESCAPE = 0x1B;
constexpr std::uint32_t kVK_TAB    = 0x09;
constexpr std::uint32_t kVK_RETURN = 0x0D;
constexpr std::uint32_t kVK_BACK   = 0x08; // Backspace

constexpr std::uint32_t kVK_INSERT = 0x2D;
constexpr std::uint32_t kVK_DELETE = 0x2E;
constexpr std::uint32_t kVK_HOME   = 0x24;
constexpr std::uint32_t kVK_END    = 0x23;
constexpr std::uint32_t kVK_PRIOR  = 0x21; // Page Up
constexpr std::uint32_t kVK_NEXT   = 0x22; // Page Down

static inline bool IsWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static std::string_view Trim(std::string_view s) noexcept
{
    while (!s.empty() && IsWhitespace(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsWhitespace(s.back())) s.remove_suffix(1);
    return s;
}

static std::string ToLowerCopy(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

static bool ReadFileToString(const std::filesystem::path& path, std::string& out) noexcept
{
    out.clear();

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz <= 0)
        return false;

    out.resize(static_cast<std::size_t>(sz));
    f.seekg(0, std::ios::beg);
    f.read(out.data(), sz);
    return f.good();
}

static std::optional<colony::input::Action> ParseActionName(std::string_view name)
{
    const std::string n = ToLowerCopy(Trim(name));

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

    return std::nullopt;
}

static std::optional<std::uint32_t> ParseInputCodeToken(std::string_view token)
{
    const std::string t = ToLowerCopy(Trim(token));
    if (t.empty())
        return std::nullopt;

    // Function keys: F1..F24
    // VK_F1 starts at 0x70.
    if (t.size() >= 2 && t[0] == 'f')
    {
        int n = 0;
        bool ok = true;
        for (std::size_t i = 1; i < t.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (!std::isdigit(c)) { ok = false; break; }
            n = n * 10 + static_cast<int>(c - '0');
            if (n > 24) { ok = false; break; }
        }

        if (ok && n >= 1 && n <= 24)
        {
            return static_cast<std::uint32_t>(0x6Fu + static_cast<std::uint32_t>(n));
        }
    }

    // Single character: treat as ASCII key. Normalize to uppercase.
    if (t.size() == 1)
    {
        const unsigned char c = static_cast<unsigned char>(t[0]);
        if (std::isalnum(c))
            return static_cast<std::uint32_t>(std::toupper(c));
    }

    // Arrow keys
    if (t == "up" || t == "arrowup") return kVK_UP;
    if (t == "down" || t == "arrowdown") return kVK_DOWN;
    if (t == "left" || t == "arrowleft") return kVK_LEFT;
    if (t == "right" || t == "arrowright") return kVK_RIGHT;

    // Common named keys
    if (t == "space" || t == "spacebar") return kVK_SPACE;

    // Common utility/navigation keys
    if (t == "esc" || t == "escape") return kVK_ESCAPE;
    if (t == "tab") return kVK_TAB;
    if (t == "enter" || t == "return") return kVK_RETURN;
    if (t == "backspace" || t == "bksp" || t == "bs") return kVK_BACK;
    if (t == "ins" || t == "insert") return kVK_INSERT;
    if (t == "del" || t == "delete") return kVK_DELETE;
    if (t == "home") return kVK_HOME;
    if (t == "end") return kVK_END;
    if (t == "pageup" || t == "pgup") return kVK_PRIOR;
    if (t == "pagedown" || t == "pgdn") return kVK_NEXT;

    // Modifiers
    if (t == "shift") return kVK_SHIFT;
    if (t == "lshift" || t == "leftshift") return kVK_LSHIFT;
    if (t == "rshift" || t == "rightshift") return kVK_RSHIFT;

    if (t == "ctrl" || t == "control") return kVK_CONTROL;
    if (t == "lctrl" || t == "leftctrl" || t == "lcontrol" || t == "leftcontrol") return kVK_LCONTROL;
    if (t == "rctrl" || t == "rightctrl" || t == "rcontrol" || t == "rightcontrol") return kVK_RCONTROL;

    if (t == "alt" || t == "menu") return kVK_MENU;
    if (t == "lalt" || t == "leftalt" || t == "lmenu" || t == "leftmenu") return kVK_LMENU;
    if (t == "ralt" || t == "rightalt" || t == "rmenu" || t == "rightmenu") return kVK_RMENU;

    // Mouse buttons (mapped into the unified input code space)
    if (t == "mouseleft" || t == "lmb" || t == "mouse1") return colony::input::kMouseButtonLeft;
    if (t == "mouseright" || t == "rmb" || t == "mouse2") return colony::input::kMouseButtonRight;
    if (t == "mousemiddle" || t == "mmb" || t == "mouse3") return colony::input::kMouseButtonMiddle;
    if (t == "mousex1" || t == "x1" || t == "mouse4" || t == "mb4") return colony::input::kMouseButtonX1;
    if (t == "mousex2" || t == "x2" || t == "mouse5" || t == "mb5") return colony::input::kMouseButtonX2;

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

static std::vector<std::string_view> Split(std::string_view s, char delim)
{
    std::vector<std::string_view> out;
    while (true)
    {
        const std::size_t pos = s.find(delim);
        if (pos == std::string_view::npos)
        {
            out.push_back(s);
            break;
        }
        out.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
    return out;
}

// Parses a chord string like: "Shift+W" or "Shift+MouseLeft".
// Returns a sorted, de-duplicated list of input codes.
static bool ParseChordString(std::string_view chordStr, std::vector<std::uint32_t>& outCodes)
{
    outCodes.clear();
    chordStr = Trim(chordStr);
    if (chordStr.empty())
        return false;

    const auto parts = Split(chordStr, '+');
    for (auto part : parts)
    {
        part = Trim(part);
        if (part.empty())
            continue;

        const auto code = ParseInputCodeToken(part);
        if (!code)
            return false;

        outCodes.push_back(*code);
    }

    if (outCodes.empty())
        return false;

    std::sort(outCodes.begin(), outCodes.end());
    outCodes.erase(std::unique(outCodes.begin(), outCodes.end()), outCodes.end());
    return !outCodes.empty();
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
    AddBinding(Action::MoveForward,  kVK_UP);

    AddBinding(Action::MoveBackward, static_cast<std::uint32_t>('S'));
    AddBinding(Action::MoveBackward, kVK_DOWN);

    AddBinding(Action::MoveLeft,     static_cast<std::uint32_t>('A'));
    AddBinding(Action::MoveLeft,     kVK_LEFT);

    AddBinding(Action::MoveRight,    static_cast<std::uint32_t>('D'));
    AddBinding(Action::MoveRight,    kVK_RIGHT);

    AddBinding(Action::MoveDown,     static_cast<std::uint32_t>('Q'));
    AddBinding(Action::MoveUp,       static_cast<std::uint32_t>('E'));

    // Example chord binding: Shift+W as a distinct action.
    {
        const std::uint32_t chord[] = { kVK_SHIFT, static_cast<std::uint32_t>('W') };
        AddBinding(Action::MoveForwardFast, std::span<const std::uint32_t>(chord, 2));
    }

    // Speed boost modifier (either shift).
    AddBinding(Action::SpeedBoost, kVK_SHIFT);
    AddBinding(Action::SpeedBoost, kVK_LSHIFT);
    AddBinding(Action::SpeedBoost, kVK_RSHIFT);

    // Mouse-driven camera actions.
    AddBinding(Action::CameraOrbit, kMouseButtonLeft);
    AddBinding(Action::CameraPan, kMouseButtonMiddle);
    AddBinding(Action::CameraPan, kMouseButtonRight);

    // Optional chord example: Shift+MouseLeft => pan.
    {
        const std::uint32_t chord[] = { kVK_SHIFT, kMouseButtonLeft };
        AddBinding(Action::CameraPan, std::span<const std::uint32_t>(chord, 2));
    }

    RecomputeActionStatesNoEvents();
}

bool InputMapper::LoadFromFile(const std::filesystem::path& path) noexcept
{
    std::string text;
    if (!ReadFileToString(path, text))
        return false;

    const auto ext = ToLowerCopy(path.extension().string());

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
        while (i < sv.size() && IsWhitespace(sv[i])) ++i;
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
            recompute = wasDown;
        }
        break;
    }

    case InputEventType::FocusLost:
        // Key/button up may never be delivered once focus is gone; clear everything and
        // emit releases for any active actions.
        m_down.reset();
        recompute = true;
        break;

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
            for (auto part : Split(bindStr, ','))
            {
                part = Trim(part);
                if (part.empty())
                    continue;

                if (!ParseChordString(part, chordCodes))
                    continue;

                parsedChords.emplace_back(chordCodes.begin(), chordCodes.end());
            }
        };

        if (v.is_string())
        {
            considerBindStr(v.get_ref<const std::string&>());
        }
        else if (v.is_array())
        {
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

        line = Trim(line);
        if (line.empty())
            continue;

        // Comments (whole-line)
        if (line.starts_with("#") || line.starts_with(";") || line.starts_with("//"))
            continue;

        // [Section]
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        // Only read bindings from [Bindings] section if present.
        if (!currentSection.empty())
        {
            const std::string secLower = ToLowerCopy(currentSection);
            if (secLower != "bindings")
                continue;
        }

        std::string_view key;
        std::string_view value;
        if (!SplitOnce(line, '=', key, value) && !SplitOnce(line, ':', key, value))
            continue;

        key = Trim(key);
        value = Trim(value);

        // Strip trailing inline comments (simple, but good enough).
        const std::size_t hashPos = value.find('#');
        const std::size_t semiPos = value.find(';');
        std::size_t cut = std::string_view::npos;
        if (hashPos != std::string_view::npos) cut = hashPos;
        if (semiPos != std::string_view::npos) cut = (cut == std::string_view::npos) ? semiPos : std::min(cut, semiPos);
        if (cut != std::string_view::npos)
            value = Trim(value.substr(0, cut));

        const auto act = ParseActionName(key);
        if (!act)
            continue;

        parsedChords.clear();

        for (auto bindStr : Split(value, ','))
        {
            bindStr = Trim(bindStr);
            if (bindStr.empty())
                continue;

            if (!ParseChordString(bindStr, chordCodes))
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
