#include "AppWindow_Impl.h"

#include <chrono>

int AppWindow::MessageLoop()
{
    MSG msg{};

    if (!m_impl)
        m_impl = std::make_unique<Impl>();

    // Match previous behavior: schedule starts "unset" and FPS timing starts when
    // the loop begins.
    m_impl->pacer.ResetSchedule();
    m_impl->pacer.ResetFps();
    m_impl->frameStats.Reset();

    bool lastVsync = m_vsync;
    bool lastUnfocused = !m_impl->active;

    auto lastPresented = std::chrono::steady_clock::now();

    while (true)
    {
        const bool unfocused = !m_impl->active;
        const bool pauseInBackground = unfocused && m_impl->settings.pauseWhenUnfocused;

        // Reset pacing when the pacing mode changes (vsync toggled, or we moved
        // between foreground/background). This prevents long sleeps after e.g.
        // Alt+Tab.
        if (lastVsync != m_vsync || lastUnfocused != unfocused)
        {
            m_impl->pacer.ResetSchedule();
            lastVsync = m_vsync;
            lastUnfocused = unfocused;
        }

        // If minimized or intentionally paused in the background, don't render;
        // block until something happens. We still consume queued input events
        // so FocusLost (etc.) reaches the game layer.
        if (m_width == 0 || m_height == 0 || pauseInBackground)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Flush any buffered mouse delta into the queue before we hand it to the game.
            m_impl->FlushPendingMouseDelta();

            const bool changed = m_impl->game.OnInput(m_impl->input.Events());
            m_impl->input.Clear();
            if (changed)
                UpdateTitle();

            // Persist any queued settings changes while we're idle/minimized.
            m_impl->MaybeAutoSaveSettings();

            // If we have a pending settings auto-save, wake up in time to write it.
            const DWORD timeoutMs = m_impl->BackgroundWaitTimeoutMs();
            MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        // Frame pacing: wait until either the next frame time arrives (when a
        // cap is active) or we receive input/messages.
        m_impl->pacer.ThrottleBeforeMessagePump(m_vsync, unfocused);

        // Pump all queued messages.
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Flush aggregated mouse movement after consuming the current burst of messages.
        m_impl->FlushPendingMouseDelta();

        const bool unfocusedAfterPump = !m_impl->active;
        const bool pauseInBackgroundAfterPump = unfocusedAfterPump && m_impl->settings.pauseWhenUnfocused;
        if (m_width == 0 || m_height == 0 || pauseInBackgroundAfterPump)
            continue;

        // If a cap is active and we woke due to messages, don't render early.
        if (!m_impl->pacer.IsTimeToRender(m_vsync, unfocusedAfterPump))
        {
            const bool changed = m_impl->game.OnInput(m_impl->input.Events());
            m_impl->input.Clear();
            if (changed)
                UpdateTitle();

            // Debounced settings persistence (non-blocking).
            m_impl->MaybeAutoSaveSettings();
            continue;
        }

        // If the swapchain exposes a frame-latency waitable object, block on it
        // (while still pumping messages) to avoid queuing ahead.
        double waitMs = 0.0;
        const HANDLE frameLatency = m_gfx.FrameLatencyWaitableObject();
        if (frameLatency != nullptr)
        {
            const auto waitStart = std::chrono::steady_clock::now();
            bool abortFrame = false;

            while (true)
            {
                const DWORD timeoutMs = m_impl->BackgroundWaitTimeoutMs();
                const DWORD r = MsgWaitForMultipleObjectsEx(1, &frameLatency, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

                if (r == WAIT_OBJECT_0)
                {
                    // Frame slot available.
                    break;
                }

                if (r == WAIT_OBJECT_0 + 1)
                {
                    // Windows messages pending.
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (msg.message == WM_QUIT)
                            return static_cast<int>(msg.wParam);
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }

                    m_impl->FlushPendingMouseDelta();

                    const bool unfocusedNow = !m_impl->active;
                    const bool pauseNow = unfocusedNow && m_impl->settings.pauseWhenUnfocused;

                    // State changed while waiting; restart the loop.
                    if (m_width == 0 || m_height == 0 || pauseNow)
                    {
                        abortFrame = true;
                        break;
                    }

                    // If a cap is active, ensure we still respect it.
                    if (!m_impl->pacer.IsTimeToRender(m_vsync, unfocusedNow))
                    {
                        abortFrame = true;
                        break;
                    }

                    continue;
                }

                if (r == WAIT_TIMEOUT)
                {
                    m_impl->MaybeAutoSaveSettings();
                    continue;
                }

                // WAIT_FAILED or unexpected return; don't hang.
                break;
            }

            waitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();

            if (abortFrame)
            {
                const bool changed = m_impl->game.OnInput(m_impl->input.Events());
                m_impl->input.Clear();
                if (changed)
                    UpdateTitle();
                m_impl->MaybeAutoSaveSettings();
                continue;
            }
        }

        // Apply input to the game as close to Present() as possible (lower latency).
        const bool changed = m_impl->game.OnInput(m_impl->input.Events());
        m_impl->input.Clear();
        if (changed)
            UpdateTitle();

        m_impl->MaybeAutoSaveSettings();

        // Render one frame.
        const DxRenderStats rs = m_gfx.Render(m_vsync);
        const auto afterRender = std::chrono::steady_clock::now();

        // If DXGI reports occlusion, avoid burning CPU/GPU. We'll yield a bit and retry.
        if (rs.occluded)
        {
            m_impl->pacer.ResetSchedule();
            MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        const double frameMs = std::chrono::duration<double, std::milli>(afterRender - lastPresented).count();
        lastPresented = afterRender;

        // PresentMon-style rolling stats (computed a few times a second).
        m_impl->frameStats.AddSample(frameMs, rs.presentMs, waitMs);
        const bool statsUpdated = m_impl->frameStats.Update(afterRender);

        // FPS counter (update about once per second).
        const bool fpsTick = m_impl->pacer.OnFramePresented(m_vsync, unfocusedAfterPump);

        if (fpsTick || (m_impl->settings.showFrameStats && statsUpdated))
            UpdateTitle();
    }
}
