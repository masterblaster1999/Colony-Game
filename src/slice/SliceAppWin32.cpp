// src/slice/SliceAppWin32.cpp

#include "SliceAppWin32.h"

#include "SliceSimulation.h"
#include "SliceRendererD3D11.h"

#include <shellapi.h>   // CommandLineToArgvW, LocalFree
#include <windowsx.h>   // GET_WHEEL_DELTA_WPARAM

#include <chrono>
#include <string>

namespace slice {

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n ? n - 1 : 0), L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

uint32_t ParseSeedArg(LPWSTR cmdLine, uint32_t defaultSeed) {
    uint32_t seed = defaultSeed;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return seed;

    for (int i = 0; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--seed") == 0) {
            seed = (uint32_t)_wtoi(argv[i + 1]);
            ++i;
        }
    }

    LocalFree(argv);
    return seed;
}

static std::wstring MMSS(double s) {
    if (s < 0.0) s = 0.0;
    int is = int(s + 0.5);
    int m = is / 60;
    int sec = is % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", m, sec);
    return buf;
}

static void UpdateWindowTitle(HWND hwnd, const SliceSimulation& sim, const SliceRendererD3D11& r) {
    std::wstring selName;
    if (!sim.orbital.Bodies().empty() && sim.selectedBody >= 0 && sim.selectedBody < (int)sim.orbital.Bodies().size()) {
        selName = Widen(sim.orbital.Bodies()[size_t(sim.selectedBody)].name);
    }

    auto hud = g_slice.hudLines();
    std::wstring objLine = hud.empty() ? L"Objective: (none)" : Widen(hud[0]);
    int pct = int(g_slice.overallProgress() * 100.0 + 0.5);
    const auto& st = g_slice.state();

    wchar_t title[512];
    swprintf_s(title,
        L"Colony Vertical Slice | FPS: %.0f (%.2f ms) | GPU: F%.2fms T%.2fms C%.2fms O%.2fms | Bodies:%zu Sel:%s | VSync:%s | TimeScale:%.2f | Seed:%u | %s | %d%% | Built:%d Crafted:%d Colonists:%d | Surv:%s",
        sim.fps.fps, sim.fps.ms,
        r.timerFrame.lastMs, r.timerTerrain.lastMs, r.timerCube.lastMs, r.timerOrbital.lastMs,
        sim.orbital.Bodies().size(), selName.c_str(),
        sim.vsync ? L"On" : L"Off", sim.timeScale, sim.seed,
        objLine.c_str(), pct, st.structuresBuilt, st.itemsCrafted, st.colonistsAlive,
        MMSS(st.elapsedSeconds).c_str());

    SetWindowTextW(hwnd, title);
}

struct AppContext {
    SliceSimulation* sim = nullptr;
    SliceRendererD3D11* renderer = nullptr;
    bool* running = nullptr;
};

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    AppContext* ctx = reinterpret_cast<AppContext*>(GetWindowLongPtrW(h, GWLP_USERDATA));

    switch (m) {
    case WM_NCCREATE: {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(l);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }

    case WM_SIZE:
        if (ctx && ctx->renderer && ctx->renderer->d.dev) {
            UINT newW = LOWORD(l);
            UINT newH = HIWORD(l);
            if (newW == 0 || newH == 0) return 0; // minimized
            ctx->renderer->resize(newW, newH);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (ctx && ctx->sim) {
            short delta = GET_WHEEL_DELTA_WPARAM(w);
            ctx->sim->onMouseWheel(delta);
        }
        return 0;

    case WM_SYSKEYDOWN:
        // Alt+Enter fullscreen toggle
        if (w == VK_RETURN && (HIWORD(l) & KF_ALTDOWN)) {
            if (ctx && ctx->renderer) ctx->renderer->toggleFullscreen();
            return 0;
        }
        break;

    case WM_DESTROY:
        if (ctx && ctx->running) *ctx->running = false;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            DestroyWindow(h);
            return 0;
        }
        return 0;
    }

    return DefWindowProcW(h, m, w, l);
}

int RunSliceApp(HINSTANCE hi, LPWSTR cmdLine, SliceSimulation& sim, SliceRendererD3D11& renderer, const SliceAppConfig& cfg) {
    const uint32_t seed = ParseSeedArg(cmdLine, sim.seed);

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"SliceWnd";
    RegisterClassW(&wc);

    bool running = true;
    AppContext ctx{ &sim, &renderer, &running };

    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"Colony Vertical Slice",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        (int)cfg.width,
        (int)cfg.height,
        nullptr,
        nullptr,
        hi,
        &ctx);

    // Initialize sim + renderer (matches the original order: after window creation)
    sim.initialize(seed);
    renderer.create(hwnd, cfg.width, cfg.height, sim);

    // Fixed-step update for determinism; render every loop
    const double dtFixed = 1.0 / 120.0;
    double acc = 0.0;
    auto tPrev = std::chrono::high_resolution_clock::now();

    MSG msg{};
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        auto tNow = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(tNow - tPrev).count();
        tPrev = tNow;
        if (dt > 0.25) dt = 0.25; // after breakpoints, etc.

        sim.fps.tick(dt);
        UpdateWindowTitle(hwnd, sim, renderer);

        acc += dt;
        while (acc >= dtFixed) {
            sim.updateSim(dtFixed);

            // Process renderer-side requests emitted by the sim.
            if (sim.requestReloadOrbitalRenderer) {
                renderer.reloadOrbitalRenderer();
                sim.requestReloadOrbitalRenderer = false;
            }
            if (sim.requestRegenerateHeight) {
                renderer.regenerateHeight(sim);
                sim.requestRegenerateHeight = false;
            }
            if (sim.requestScreenshot) {
                renderer.saveScreenshotBMP();
                sim.requestScreenshot = false;
            }

            acc -= dtFixed;
        }

        float clear[4] = { 0.06f, 0.09f, 0.12f, 1.f };
        renderer.beginFrame(clear);
        renderer.renderFrame(sim);
        renderer.present(sim.vsync);
    }

    return 0;
}

} // namespace slice
