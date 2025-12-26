#include "AppWindow_Impl.h"

#include <algorithm>
#include <chrono>

#if defined(COLONY_WITH_IMGUI)
    #include <imgui.h>
#endif

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

    // First rendered frame will initialize dt tracking.
    m_impl->hasLastRenderTick = false;

    bool lastVsync = m_vsync;
    bool lastUnfocused = !m_impl->active;

    auto lastPresented = std::chrono::steady_clock::now();

    int exitCode = 0;

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

            // Also reset our per-render dt so we don't simulate a huge step after Alt+Tab.
            m_impl->hasLastRenderTick = false;
        }

        // If minimized or intentionally paused in the background, don't render;
        // block until something happens. We still consume queued input events
        // so FocusLost (etc.) reaches the game layer.
        if (m_width == 0 || m_height == 0 || pauseInBackground)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    exitCode = static_cast<int>(msg.wParam);
                    goto exit_loop;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Flush any buffered mouse delta into the queue before we hand it to the game.
            m_impl->FlushPendingMouseDelta();

#if defined(COLONY_WITH_IMGUI)
            const bool uiWantsKeyboard = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsKeyboard() : false;
            const bool uiWantsMouse = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsMouse() : false;
#else
            const bool uiWantsKeyboard = false;
            const bool uiWantsMouse = false;
#endif

            const bool changed = m_impl->game.OnInput(m_impl->input.Events(), uiWantsKeyboard, uiWantsMouse);
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
            {
                exitCode = static_cast<int>(msg.wParam);
                goto exit_loop;
            }
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
#if defined(COLONY_WITH_IMGUI)
            const bool uiWantsKeyboard = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsKeyboard() : false;
            const bool uiWantsMouse = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsMouse() : false;
#else
            const bool uiWantsKeyboard = false;
            const bool uiWantsMouse = false;
#endif

            const bool changed = m_impl->game.OnInput(m_impl->input.Events(), uiWantsKeyboard, uiWantsMouse);
            m_impl->input.Clear();
            if (changed)
                UpdateTitle();

            // Debounced settings persistence (non-blocking).
            m_impl->MaybeAutoSaveSettings();
            continue;
        }

        // If the swapchain exposes a frame-latency waitable object, block on it
        // (while still pumping messages) to avoid queuing ahead.
        //
        // NOTE: The waitable handle can legitimately change when the swapchain is resized
        // (WM_SIZE / WM_EXITSIZEMOVE) or when we adjust the maximum frame latency (F8),
        // because DxDevice will close the old handle and obtain a new one.
        // Calling MsgWaitForMultipleObjectsEx on a handle that has since been closed is
        // undefined behavior, so we re-query the handle each time through the loop.
        double waitMs = 0.0;
        if (m_gfx.HasFrameLatencyWaitableObject())
        {
            bool abortFrame = false;

            while (true)
            {
                // Re-fetch every iteration in case message dispatch resized the swapchain.
                HANDLE frameLatency = m_gfx.FrameLatencyWaitableObject();
                if (frameLatency == nullptr)
                    break;

                const DWORD timeoutMs = m_impl->BackgroundWaitTimeoutMs();

                const auto callStart = std::chrono::steady_clock::now();
                const DWORD r = MsgWaitForMultipleObjectsEx(1, &frameLatency, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                waitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callStart).count();

                if (r == WAIT_OBJECT_0)
                {
                    // Frame slot available.
                    //
                    // MsgWaitForMultipleObjectsEx prioritizes signaled handles over messages
                    // (it returns the first signaled object in pHandles). That means there can
                    // be pending input in the queue even though we "won" on the handle.
                    // Drain messages now so input is applied as close to Present() as possible.
                    bool pumped = false;
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        pumped = true;
                        if (msg.message == WM_QUIT)
                        {
                            exitCode = static_cast<int>(msg.wParam);
                            goto exit_loop;
                        }
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }

                    m_impl->FlushPendingMouseDelta();

                    const bool unfocusedNow = !m_impl->active;
                    const bool pauseNow = unfocusedNow && m_impl->settings.pauseWhenUnfocused;

                    // State changed while waiting; restart the outer loop.
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

                    // If we pumped messages, the swapchain / waitable handle may have changed.
                    // Re-evaluate before rendering so we don't queue against the wrong swapchain.
                    if (pumped)
                        continue;

                    break;
                }

                if (r == WAIT_OBJECT_0 + 1)
                {
                    // Windows messages pending.
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (msg.message == WM_QUIT)
                        {
                            exitCode = static_cast<int>(msg.wParam);
                            goto exit_loop;
                        }
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }

                    m_impl->FlushPendingMouseDelta();

                    const bool unfocusedNow = !m_impl->active;
                    const bool pauseNow = unfocusedNow && m_impl->settings.pauseWhenUnfocused;

                    // State changed while waiting; restart the outer loop.
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

                // WAIT_FAILED, WAIT_IO_COMPLETION, or unexpected return; don't hang.
                break;
            }

            if (abortFrame)
            {
#if defined(COLONY_WITH_IMGUI)
                const bool uiWantsKeyboard = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsKeyboard() : false;
                const bool uiWantsMouse = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsMouse() : false;
#else
                const bool uiWantsKeyboard = false;
                const bool uiWantsMouse = false;
#endif

                const bool changed = m_impl->game.OnInput(m_impl->input.Events(), uiWantsKeyboard, uiWantsMouse);
                m_impl->input.Clear();
                if (changed)
                    UpdateTitle();
                m_impl->MaybeAutoSaveSettings();
                continue;
            }
        }

        // --- Render path ---

        const auto frameStart = std::chrono::steady_clock::now();

        float dtSeconds = 0.0f;
        if (m_impl->hasLastRenderTick)
            dtSeconds = std::chrono::duration<float>(frameStart - m_impl->lastRenderTick).count();
        m_impl->lastRenderTick = frameStart;
        m_impl->hasLastRenderTick = true;

        // Prevent giant simulation steps after stalls.
        dtSeconds = std::clamp(dtSeconds, 0.0f, 0.25f);

        // Clear + set RT/viewport.
        m_gfx.BeginFrame();

#if defined(COLONY_WITH_IMGUI)
        if (m_impl->imguiReady && m_impl->imgui.enabled)
            m_impl->imgui.newFrame();

        const bool uiWantsKeyboard = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsKeyboard() : false;
        const bool uiWantsMouse = (m_impl->imguiReady && m_impl->imgui.enabled) ? m_impl->imgui.wantsMouse() : false;
#else
        const bool uiWantsKeyboard = false;
        const bool uiWantsMouse = false;
#endif

        // Apply input to the game as close to Present() as possible (lower latency).
        const bool inputChanged = m_impl->game.OnInput(m_impl->input.Events(), uiWantsKeyboard, uiWantsMouse);
        m_impl->input.Clear();

        // Simulate + update per-frame state.
        const bool updateChanged = m_impl->game.Update(dtSeconds, uiWantsKeyboard, uiWantsMouse);

        if (inputChanged || updateChanged)
            UpdateTitle();

#if defined(COLONY_WITH_IMGUI)
        if (m_impl->imguiReady && m_impl->imgui.enabled)
        {
            m_impl->game.DrawUI();
            m_impl->imgui.render();
        }
#endif

        m_impl->MaybeAutoSaveSettings();

        // Present.
        const DxRenderStats rs = m_gfx.EndFrame(m_vsync);
        const auto afterRender = std::chrono::steady_clock::now();

#if defined(COLONY_WITH_IMGUI)
        if (m_impl->imguiReady && m_gfx.ConsumeDeviceRecreatedFlag())
        {
            m_impl->imgui.shutdown();
            m_impl->imguiReady = m_impl->imgui.initialize(m_hwnd, m_gfx.Device(), m_gfx.Context());
        }
#endif

        // If DXGI reports occlusion, avoid burning CPU/GPU. We'll yield a bit and retry.
        if (rs.occluded)
        {
            m_impl->pacer.ResetSchedule();
            m_impl->hasLastRenderTick = false;
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

exit_loop:

#if defined(COLONY_WITH_IMGUI)
    if (m_impl && m_impl->imguiReady)
    {
        m_impl->imgui.shutdown();
        m_impl->imguiReady = false;
    }
#endif

    return exitCode;
}
