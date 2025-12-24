#include "game/PrototypeGame.h"

#include "input/InputBindingParse.h"
#include "input/InputMapper.h"
#include "loop/DebugCamera.h"

#include "platform/win/LauncherLogSingletonWin.h"
#include "platform/win/WinFiles.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Best-effort logging to the same process-wide log stream used by AppMain/launcher.
static void LogLine(const std::wstring& line) noexcept
{
    try
    {
        auto& log = LauncherLog();
        WriteLog(log, line);
    }
    catch (...)
    {
        // Logging must never take down the game.
    }
}

static void AddUnique(std::vector<fs::path>& out, const fs::path& p)
{
    const auto ps = p.generic_wstring();
    for (const auto& e : out)
    {
        if (e.generic_wstring() == ps)
            return;
    }
    out.push_back(p);
}

static std::vector<fs::path> BuildBindingsCandidates()
{
    std::vector<fs::path> out;

    // 0) User override (per-machine/per-user) in LocalAppData\ColonyGame.
    //    This lets players change bindings without touching the install folder
    //    (and avoids write permissions issues in Program Files).
    const fs::path saveDir = platform::win::GetSaveDir();
    if (!saveDir.empty())
    {
        AddUnique(out, saveDir / L"input_bindings.json");
        AddUnique(out, saveDir / L"input_bindings.ini");
    }

    // 1) Dev-friendly search: walk up from the current working directory.
    //    This mirrors InputMapper::LoadFromDefaultPaths(), but we need the
    //    successful path for logging + hot-reload.
    constexpr int kMaxParents = 5;
    std::error_code ec;

    fs::path base = fs::current_path(ec);
    if (ec || base.empty())
        base = fs::path(L".");

    for (int depth = 0; depth <= kMaxParents; ++depth)
    {
        AddUnique(out, base / L"assets" / L"config" / L"input_bindings.json");
        AddUnique(out, base / L"assets" / L"config" / L"input_bindings.ini");
        AddUnique(out, base / L"input_bindings.json");
        AddUnique(out, base / L"input_bindings.ini");

        if (!base.has_parent_path())
            break;
        const auto parent = base.parent_path();
        if (parent == base)
            break;
        base = parent;
    }

    // 2) Shipping-friendly search: paths relative to the executable.
    const fs::path exeDir = platform::win::GetExeDir();
    if (!exeDir.empty())
    {
        AddUnique(out, exeDir / L"assets" / L"config" / L"input_bindings.json");
        AddUnique(out, exeDir / L"assets" / L"config" / L"input_bindings.ini");
        AddUnique(out, exeDir / L"input_bindings.json");
        AddUnique(out, exeDir / L"input_bindings.ini");

        // Common "bin next to repo" layouts.
        AddUnique(out, exeDir.parent_path() / L"assets" / L"config" / L"input_bindings.json");
        AddUnique(out, exeDir.parent_path() / L"assets" / L"config" / L"input_bindings.ini");
    }

    return out;
}

static bool TryGetLastWriteTime(const fs::path& p, fs::file_time_type& outTime) noexcept
{
    std::error_code ec;
    outTime = fs::last_write_time(p, ec);
    return !ec;
}

static bool TryLoadBindings(colony::input::InputMapper& mapper,
                            const std::vector<fs::path>& candidates,
                            fs::path& outLoadedPath) noexcept
{
    std::error_code ec;
    for (const auto& p : candidates)
    {
        ec.clear();
        if (!fs::exists(p, ec) || ec)
            continue;

        if (mapper.LoadFromFile(p))
        {
            outLoadedPath = p;
            return true;
        }

        // File exists but did not parse.
        LogLine(L"[Input] Failed to parse bindings file: " + p.wstring());
    }
    return false;
}

} // namespace

namespace colony::game {

struct PrototypeGame::Impl {
    colony::appwin::DebugCameraController camera;
    colony::input::InputMapper            mapper;

    // Input bindings hot-reload
    std::vector<fs::path>                 bindingsCandidates;
    fs::path                              bindingsPath;
    fs::file_time_type                    bindingsWriteTime{};
    fs::file_time_type                    lastFailedWriteTime{};
    bool                                  hasBindingsPath = false;
    bool                                  hasBindingsWriteTime = false;
    bool                                  hasLastFailedWriteTime = false;
    bool                                  loggedMissing = false;
    float                                 bindingsPollAccum = 0.f;   // seconds
    float                                 bindingsSearchAccum = 0.f; // seconds
};

static void HotReloadBindings(PrototypeGame::Impl& impl, float dtSeconds, bool force) noexcept
{
    // Drive the timers from the fixed-step simulation dt. This is good enough
    // for a prototype and keeps the logic simple.
    if (dtSeconds > 0.0f)
    {
        impl.bindingsPollAccum += dtSeconds;
        impl.bindingsSearchAccum += dtSeconds;
    }

    constexpr float kPollPeriod = 0.5f;  // seconds
    constexpr float kSearchPeriod = 2.0f; // seconds

    const bool shouldPoll = force || (impl.bindingsPollAccum >= kPollPeriod);
    const bool shouldSearch = force || (!impl.hasBindingsPath && (impl.bindingsSearchAccum >= kSearchPeriod));

    if (!shouldPoll && !shouldSearch)
        return;

    if (shouldPoll)
        impl.bindingsPollAccum = 0.0f;
    if (shouldSearch)
        impl.bindingsSearchAccum = 0.0f;

    if (force)
        colony::LogLine(L"[Input] Reload bindings requested");

    // Poll current file if we have one.
    if (impl.hasBindingsPath && shouldPoll)
    {
        if (auto now = TryGetLastWriteTime(impl.bindingsPath))
        {
            if (!impl.hasBindingsWriteTime || (*now != impl.bindingsWriteTime))
            {
                if (TryLoadBindings(impl.bindingsPath, impl.mapper))
                {
                    impl.bindingsWriteTime = *now;
                    impl.hasBindingsWriteTime = true;
                }
            }
        }
        return;
    }

    // If we don't have a bindings path yet, periodically search candidates.
    if (!impl.hasBindingsPath && shouldSearch)
    {
        for (const auto& p : impl.bindingsCandidates)
        {
            if (TryLoadBindings(p, impl.mapper))
            {
                impl.bindingsPath = p;
                impl.hasBindingsPath = true;

                if (auto ft = TryGetLastWriteTime(p))
                {
                    impl.bindingsWriteTime = *ft;
                    impl.hasBindingsWriteTime = true;
                }
                break;
            }
        }
    }
}

PrototypeGame::PrototypeGame()
{
    m_impl.reset(new (std::nothrow) Impl{});

    // Optional: allow developers to override bindings without recompiling.
    // If no config file is found, defaults remain.
    if (!m_impl)
        return;

    m_impl->bindingsCandidates = BuildBindingsCandidates();

    fs::path loaded;
    if (TryLoadBindings(m_impl->mapper, m_impl->bindingsCandidates, loaded))
    {
        m_impl->bindingsPath = loaded;
        m_impl->hasBindingsPath = true;

        fs::file_time_type ft{};
        if (TryGetLastWriteTime(loaded, ft))
        {
            m_impl->bindingsWriteTime = ft;
            m_impl->hasBindingsWriteTime = true;
        }

        LogLine(L"[Input] Loaded bindings: " + loaded.wstring());
    }
    else
    {
        LogLine(L"[Input] No input_bindings.json/.ini found (using compiled defaults)."
                L" Expected e.g. assets\\config\\input_bindings.json");
    }
}

PrototypeGame::~PrototypeGame()
    = default;

bool PrototypeGame::OnInput(std::span<const colony::input::InputEvent> events) noexcept
{
    if (!m_impl)
        return false;

    bool changed = false;
    bool actionsChanged = false;

    // Process events in order so action-chords + mouse-drag decisions are made
    // against the *current* button state (not just the final state for the frame).
    m_impl->mapper.BeginFrame();

    for (const auto& ev : events)
    {
        if (m_impl->mapper.ConsumeEvent(ev))
            actionsChanged = true;

        using colony::input::InputEventType;
        switch (ev.type)
        {
        case InputEventType::MouseDelta:
        {
            // Orbit/Pan are action-driven (mouse buttons are bound through InputMapper).
            // If both actions are down (e.g., due to an overlapping bind), prefer pan.
            const bool pan = m_impl->mapper.IsDown(colony::input::Action::CameraPan);
            const bool orbit = m_impl->mapper.IsDown(colony::input::Action::CameraOrbit) && !pan;

            if (m_impl->camera.ApplyDrag(static_cast<long>(ev.dx), static_cast<long>(ev.dy), orbit, pan))
                changed = true;
            break;
        }

        case InputEventType::MouseWheel:
            if (m_impl->camera.ApplyWheelDetents(static_cast<int>(ev.wheelDetents)))
                changed = true;
            break;

        case InputEventType::FocusLost:
            // Drop all held actions when we lose focus to avoid "stuck key" symptoms.
            m_impl->mapper.ClearState();
            actionsChanged = true;
            changed = true;
            break;

        default:
            break;
        }
    }

    if (actionsChanged)
        changed = true;

    // Manual input bindings hot-reload (defaults to F5). Automatic polling is
    // handled in UpdateFixed().
    bool reloadRequested = false;
    for (const auto& ae : m_impl->mapper.ActionEvents())
    {
        if (ae.action == colony::input::Action::ReloadBindings &&
            ae.type == colony::input::ActionEventType::Pressed)
        {
            reloadRequested = true;
            break;
        }
    }

    HotReloadBindings(*m_impl, 0.f, reloadRequested);

    return changed;
}

bool PrototypeGame::UpdateFixed(float dtSeconds) noexcept
{
    if (!m_impl)
        return false;

    // Automatic hot-reload polling (filesystem timestamps).
    HotReloadBindings(*m_impl, dtSeconds, false);

    // Continuous keyboard movement (WASD + QE) in camera-relative space.
    const auto axes = m_impl->mapper.GetMovementAxes();
    const bool anyMove = (axes.x != 0.f) || (axes.y != 0.f) || (axes.z != 0.f);
    if (!anyMove || dtSeconds <= 0.f)
        return false;

    const auto& s = m_impl->camera.State();
    constexpr float kPi = 3.14159265358979323846f;
    const float yawRad = s.yaw * (kPi / 180.f);
    const float sinY = std::sin(yawRad);
    const float cosY = std::cos(yawRad);

    // Forward when yaw==0 is +Y. Right is +X.
    const float fwdX = sinY;
    const float fwdY = cosY;
    const float rightX = cosY;
    const float rightY = -sinY;

    // Boost can be a modifier action, or it can be implied by chord actions.
    const bool boost = m_impl->mapper.IsDown(colony::input::Action::SpeedBoost) ||
                       m_impl->mapper.IsDown(colony::input::Action::MoveForwardFast);
    const float speedMul = boost ? 3.0f : 1.0f;

    // Pan speed is "world" units per second. Tune later.
    constexpr float kPanSpeed = 3.0f;
    const float panSpeed = kPanSpeed * speedMul;
    const float worldX = (rightX * axes.x + fwdX * axes.y) * (panSpeed * dtSeconds);
    const float worldY = (rightY * axes.x + fwdY * axes.y) * (panSpeed * dtSeconds);

    bool moved = false;
    if (m_impl->camera.ApplyPan(worldX, worldY))
        moved = true;

    if (axes.z != 0.f)
    {
        // Exponential zoom is stable (always positive) and feels consistent.
        constexpr float kZoomSpeed = 1.5f; // per second
        const float zoomSpeed = kZoomSpeed * (boost ? 2.0f : 1.0f);
        const float factor = std::exp(axes.z * zoomSpeed * dtSeconds);
        if (m_impl->camera.ApplyZoomFactor(factor))
            moved = true;
    }

    return moved;
}

DebugCameraInfo PrototypeGame::GetDebugCameraInfo() const noexcept
{
    DebugCameraInfo out{};
    if (!m_impl)
        return out;

    const auto& s = m_impl->camera.State();
    out.yaw = s.yaw;
    out.pitch = s.pitch;
    out.panX = s.panX;
    out.panY = s.panY;
    out.zoom = s.zoom;
    return out;
}

} // namespace colony::game
