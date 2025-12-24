#include "AppWindow_Internal.h"

#include "core/Log.h"

#include <cmath>

#if defined(COLONY_WITH_IMGUI)
#include <imgui.h>
#endif

int AppWindow::MessageLoop()
{
    MSG msg{};

    if (!m_impl)
        return 0;

    m_impl->pacer.ResetSchedule();
    m_impl->pacer.ResetFps();

    // Initialize the simulation clock.
    m_impl->simClock.Reset();
    m_impl->simClockInitialized = true;
    m_impl->simAccumulator = 0.0;
    m_impl->simTimeSeconds = 0.0;

    // Debounced autosave: only write after settings stop changing for a bit.
    constexpr auto kAutoSaveDelay = std::chrono::milliseconds(750);

    auto maybeAutosaveSettings = [&]() {
        if (!m_impl || !m_impl->settingsDirty)
            return;

        // Avoid disk IO while the user is actively dragging the resize grip.
        if (m_impl->inSizeMove)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now - m_impl->settingsDirtySince < kAutoSaveDelay)
            return;

        if (colony::appwin::SaveUserSettings(m_impl->settings))
        {
            m_impl->settingsDirty = false;
            colony::LogLine(L"[Settings] Autosaved");
        }
    };

#if defined(COLONY_WITH_IMGUI)
    auto drawOverlayUI = [&]() {
        if (!m_impl)
            return;
        if (!m_impl->imguiInitialized || !m_impl->imgui.enabled || !m_impl->overlayVisible)
            return;

        // Small pinned overlay in the top-left.
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav;

        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);

        if (ImGui::Begin("##ColonyOverlay", nullptr, flags))
        {
            const auto cam = m_impl->game.GetDebugCameraInfo();

            ImGui::Text("Colony Prototype");
            ImGui::Separator();

            ImGui::Text("FPS: %.1f", m_impl->pacer.Fps());
            ImGui::Text("VSync: %s", m_vsync ? "ON" : "OFF");
            ImGui::Text("Fullscreen: %s", m_impl->fullscreen.IsFullscreen() ? "ON" : "OFF");

            if (!m_impl->active)
                ImGui::TextDisabled("(unfocused)");

            ImGui::Separator();

            ImGui::Text("Sim: %.1f Hz (dt %.4f)", m_impl->simTickHz, m_impl->simFixedDt);
            ImGui::Text("Frame dt: %.4f (clamped %.4f)", m_impl->simFrameDt, m_impl->simClampedDt);
            ImGui::Text("Ticks: %d  alpha: %.2f", m_impl->simTicksLastFrame, m_impl->simAlpha);

            if (m_impl->simDroppedTimeThisFrame)
                ImGui::TextDisabled("Dropped backlog: %.3fs", m_impl->simDroppedSecondsThisFrame);

            ImGui::Separator();

            ImGui::Text("Cam yaw %.0f pitch %.0f dist %.1f", cam.yawDeg, cam.pitchDeg, cam.distance);

            ImGui::Separator();

            // Controls
            bool vs = m_vsync;
            if (ImGui::Checkbox("VSync", &vs))
                ToggleVsync();

            bool fs = m_impl->fullscreen.IsFullscreen();
            if (ImGui::Checkbox("Fullscreen", &fs))
                ToggleFullscreen();

            ImGui::Spacing();

            if (ImGui::Button(m_impl->simPaused ? "Resume" : "Pause"))
                m_impl->simPaused = !m_impl->simPaused;
            ImGui::SameLine();
            if (ImGui::Button("Step"))
                ++m_impl->simStepRequests;

            float timeScale = m_impl->simTimeScale;
            if (ImGui::SliderFloat("Time scale", &timeScale, 0.0f, 4.0f, "%.2f"))
            {
                m_impl->simTimeScale = timeScale;
                m_impl->settings.simTimeScale = timeScale;
                MarkSettingsDirty();
            }

            // Tick rate editing (int slider to keep the UI simple).
            int tickHz = static_cast<int>(m_impl->simTickHz + 0.5);
            if (ImGui::SliderInt("Tick Hz", &tickHz, 10, 240))
            {
                m_impl->simTickHz = static_cast<double>(tickHz);
                m_impl->simFixedDt = 1.0 / m_impl->simTickHz;
                m_impl->settings.simTickHz = m_impl->simTickHz;

                // Reset accumulator to avoid a single giant catch-up burst when changing dt.
                m_impl->simAccumulator = 0.0;
                m_impl->simClock.Reset();

                MarkSettingsDirty();
            }

            int maxSteps = m_impl->simMaxStepsPerFrame;
            if (ImGui::SliderInt("Max catch-up", &maxSteps, 1, 32))
            {
                m_impl->simMaxStepsPerFrame = maxSteps;
                m_impl->settings.simMaxStepsPerFrame = maxSteps;
                MarkSettingsDirty();
            }

            float maxFrameDt = static_cast<float>(m_impl->simMaxFrameDt);
            if (ImGui::SliderFloat("Max frame dt", &maxFrameDt, 0.01f, 0.5f, "%.3f"))
            {
                m_impl->simMaxFrameDt = static_cast<double>(maxFrameDt);
                m_impl->settings.simMaxFrameDt = m_impl->simMaxFrameDt;
                MarkSettingsDirty();
            }

            ImGui::Separator();
            ImGui::TextDisabled("F1 toggles overlay");
        }
        ImGui::End();

        if (m_impl->showImGuiDemo)
            ImGui::ShowDemoWindow(&m_impl->showImGuiDemo);
    };
#endif

    // Update the title on a small cadence even when FPS isn't ready yet.
    double titleCadenceAccum = 0.0;

    while (true)
    {
        const bool minimized = IsIconic(m_hwnd) != FALSE;
        const bool background = !m_impl->active;
        const bool pauseWhenUnfocused = m_impl->settings.pauseWhenUnfocused;
        const bool pauseDueToFocus = background && pauseWhenUnfocused;

        if (minimized || pauseDueToFocus)
        {
            WaitMessage();
            // Don't accumulate a massive dt when we come back.
            m_impl->simClock.Reset();
        }

        m_impl->pacer.ThrottleBeforeMessagePump(m_vsync, background && !pauseWhenUnfocused);

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return static_cast<int>(msg.wParam);

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Feed input into the game once per frame.
        const bool inputWantsTitleRefresh = m_impl->game.OnInput(m_impl->input.Events());
        m_impl->input.Clear();

        // Only render when our pacer says it's time.
        if (!m_impl->pacer.IsTimeToRender(m_vsync, background && !pauseWhenUnfocused))
        {
            maybeAutosaveSettings();
            continue;
        }

        // ------------------------------------------------------------
        // Fixed-step simulation update
        // ------------------------------------------------------------
        m_impl->simDroppedTimeThisFrame = false;
        m_impl->simDroppedSecondsThisFrame = 0.0;

        double frameDt = m_impl->simClock.Tick();
        m_impl->simFrameDt = frameDt;

        double clampedDt = frameDt;
        if (clampedDt > m_impl->simMaxFrameDt)
            clampedDt = m_impl->simMaxFrameDt;
        if (clampedDt < 0.0)
            clampedDt = 0.0;

        m_impl->simClampedDt = clampedDt;

        const bool simPaused = minimized || pauseDueToFocus || m_impl->simPaused;

        int ticksThisFrame = 0;

        if (simPaused)
        {
            // While paused, we don't accumulate real time. We still allow manual stepping.
            m_impl->simAccumulator = 0.0;

            while (m_impl->simStepRequests > 0)
            {
                const float dtSim = static_cast<float>(m_impl->simFixedDt * m_impl->simTimeScale);
                if (m_impl->game.UpdateFixed(dtSim))
                {
                    // Nothing needed here yet; leaving hook for future systems.
                }

                m_impl->simTimeSeconds += m_impl->simFixedDt * m_impl->simTimeScale;
                --m_impl->simStepRequests;
                ++ticksThisFrame;
            }

            m_impl->simAlpha = 0.0;
        }
        else
        {
            m_impl->simAccumulator += clampedDt;

            const double dtReal = m_impl->simFixedDt;
            const int maxSteps = m_impl->simMaxStepsPerFrame;

            while (m_impl->simAccumulator >= dtReal && ticksThisFrame < maxSteps)
            {
                const float dtSim = static_cast<float>(dtReal * m_impl->simTimeScale);
                m_impl->game.UpdateFixed(dtSim);

                m_impl->simTimeSeconds += dtReal * m_impl->simTimeScale;
                m_impl->simAccumulator -= dtReal;
                ++ticksThisFrame;
            }

            if (ticksThisFrame == maxSteps && m_impl->simAccumulator >= dtReal)
            {
                // Spiral-of-death guard: drop the backlog.
                const double remaining = m_impl->simAccumulator;
                const double snapped = std::fmod(remaining, dtReal);
                m_impl->simDroppedTimeThisFrame = true;
                m_impl->simDroppedSecondsThisFrame = remaining - snapped;
                m_impl->simAccumulator = snapped;
            }

            m_impl->simAlpha = (dtReal > 0.0) ? (m_impl->simAccumulator / dtReal) : 0.0;
            if (m_impl->simAlpha < 0.0)
                m_impl->simAlpha = 0.0;
            if (m_impl->simAlpha > 1.0)
                m_impl->simAlpha = 1.0;
        }

        m_impl->simTicksLastFrame = ticksThisFrame;

        // ------------------------------------------------------------
        // Render
        // ------------------------------------------------------------
        m_gfx.BeginFrame();

#if defined(COLONY_WITH_IMGUI)
        if (m_impl->imguiInitialized && m_gfx.ConsumeDeviceRecreatedFlag())
        {
            // Device was recreated after a device-lost; reinitialize ImGui's device objects.
            colony::LogLine(L"[ImGui] Reinitializing after device recreation");
            m_impl->imgui.shutdown();
            m_impl->imguiInitialized = m_impl->imgui.initialize(m_hwnd, m_gfx.Device(), m_gfx.Context());
        }

        if (m_impl->imguiInitialized && m_impl->imgui.enabled && m_impl->overlayVisible)
        {
            m_impl->imgui.newFrame();
            drawOverlayUI();
            m_impl->imgui.render();
        }
#endif

        m_gfx.EndFrame(m_vsync);

        const bool fpsUpdated = m_impl->pacer.OnFramePresented(m_vsync, background && !pauseWhenUnfocused);

        // Title cadence: once per ~0.25s, plus any time we got an explicit refresh request.
        titleCadenceAccum += clampedDt;
        if (fpsUpdated || inputWantsTitleRefresh || titleCadenceAccum >= 0.25)
        {
            UpdateTitle();
            titleCadenceAccum = 0.0;
        }

        maybeAutosaveSettings();
    }
}
