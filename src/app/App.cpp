#include "App.h"
#include <chrono>

// --- Small helper: monotonic time in seconds (for FixedStepper) ---
static double NowSeconds()
{
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    const auto now = clock::now();
    return std::chrono::duration<double>(now - t0).count();
}

App::App() = default;

LRESULT CALLBACK App::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_CREATE:
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    return 0;

    case WM_SIZE:
        if (self) {
            UINT w = LOWORD(lParam);
            UINT h = HIWORD(lParam);
            if (w && h) self->OnResize(w, h);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool App::CreateMainWindow(HINSTANCE hInst, int nCmdShow)
{
    const wchar_t* clsName = L"ColonyGameWndClass";

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = App::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = clsName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    RECT rc{ 0,0,(LONG)m_width,(LONG)m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0, clsName, L"Colony Game", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, this);

    ShowWindow(m_hwnd, nCmdShow);
    return m_hwnd != nullptr;
}

int App::Run(HINSTANCE hInstance)
{
    if (!CreateMainWindow(hInstance, SW_SHOWDEFAULT))
        return -1;

    // Initialize D3D11
    m_gfx.Initialize(m_hwnd, m_width, m_height,
    #if defined(_DEBUG)
        true
    #else
        false
    #endif
    );

    // Initialize erosion (compute)
    m_erosion.Initialize(m_gfx.Dev(), m_gfx.Ctx(), m_width, m_height, L"shaders");

    // Initialize fixed stepper time base
    m_step.reset(NowSeconds());

    MSG msg{};
    const float clear[4] = { 0.06f, 0.07f, 0.09f, 1.0f };

    for (;;)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Drive fixed-step simulation + one render per frame
        auto stats = m_step.step(
            NowSeconds(),
            // update(dt):
            [this](double /*dt*/)
            {
                terrain::ErodeParams p{};
                p.Talus    = 0.02f;
                p.Strength = 0.5f;
                m_erosion.Step(p, 1);
            },
            // render(alpha):
            [this, &clear](float /*alpha*/)
            {
                m_gfx.BeginFrame(clear);
                // (Optional) draw a full-screen pass reading m_erosion.HeightSRV()
                m_gfx.EndFrame(1);
                // Or call Render(); if/when you add custom drawing
                // Render();
            }
        );

        (void)stats; // stats.ticks_this_frame, stats.alpha available if needed
    }
}

void App::OnResize(UINT w, UINT h)
{
    m_width = w;
    m_height = h;
    m_gfx.Resize(w, h);

    // If you want erosion textures to match backbuffer size, reinitialize:
    // m_erosion.Initialize(m_gfx.Dev(), m_gfx.Ctx(), w, h, L"shaders");
}

void App::Update(double /*dt*/)
{
    // Hook for future CPU-side sim work.
}

void App::Render()
{
    // If you add a blit of HeightSRV() here, put it between BeginFrame & EndFrame.
}
