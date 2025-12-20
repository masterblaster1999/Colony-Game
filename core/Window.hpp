// core/Window.hpp
#pragma once

#if !defined(_WIN32)
  #error "Colony-Game targets Windows only."
#endif

// Keep Windows headers lean + avoid min/max macro pollution.
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace core
{
  // A small Win32 window wrapper that matches what core/Application.cpp probes for:
  //  - pollMessages() (message pump)
  //  - shouldClose() / isOpen()
  //  - constructors that accept HINSTANCE and optionally size + title
  //
  // It intentionally does NOT do any rendering/presenting: your renderer should
  // present via swapchain (DX) or its own mechanism.
  class Window final
  {
  public:
    struct CreateInfo
    {
      HINSTANCE   hInstance      = nullptr;
      int         clientWidth    = 1280;
      int         clientHeight   = 720;
      std::wstring title         = L"Colony Game";
      bool        resizable      = true;
      bool        visible        = true;
      bool        startMaximized = false;
      bool        acceptFileDrops = false;

      // DPI awareness is process-global. If true, we attempt to enable per-monitor DPI
      // before creating the first window (best-effort; safe if unavailable).
      bool        enablePerMonitorDpi = true;
    };

    Window() = default;

    // Satisfies MakeWindow() probe: W{ hInstance }
    explicit Window(HINSTANCE hInstance)
    {
      CreateInfo ci;
      ci.hInstance = hInstance;
      create(ci);
    }

    // Satisfies MakeWindow() probe: W{ hInstance, 1280, 720, L"Colony Game" }
    Window(HINSTANCE hInstance, int width, int height, const wchar_t* title)
    {
      CreateInfo ci;
      ci.hInstance = hInstance;
      ci.clientWidth = width;
      ci.clientHeight = height;
      if (title && *title)
        ci.title = title;
      create(ci);
    }

    // Satisfies MakeWindow() probe: W{ hInstance, L"Colony Game", 1280, 720 }
    Window(HINSTANCE hInstance, const wchar_t* title, int width, int height)
      : Window(hInstance, width, height, title)
    {
    }

    // Optional alternative probe: W::Create(hInstance)
    static Window Create(HINSTANCE hInstance)
    {
      return Window(hInstance);
    }

    ~Window() noexcept
    {
      // Best-effort cleanup. DestroyWindow is safe to call only when hwnd_ is valid.
      // WM_NCDESTROY clears hwnd_ for us; if already cleared, this does nothing.
      try { destroy(); } catch (...) {}
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept
    {
      moveFrom(std::move(other));
    }

    Window& operator=(Window&& other) noexcept
    {
      if (this != &other)
      {
        try { destroy(); } catch (...) {}
        moveFrom(std::move(other));
      }
      return *this;
    }

    // Probe: default construct + w.create(hInstance)
    void create(HINSTANCE hInstance)
    {
      CreateInfo ci;
      ci.hInstance = hInstance;
      create(ci);
    }

    void create(const CreateInfo& ci)
    {
      if (hwnd_ != nullptr)
        throw std::logic_error("core::Window::create called twice on the same Window instance.");

      if (!ci.hInstance)
        throw std::invalid_argument("core::Window::create requires a valid HINSTANCE.");

      hinstance_ = ci.hInstance;
      title_ = ci.title.empty() ? L"Colony Game" : ci.title;

      if (ci.enablePerMonitorDpi)
        EnablePerMonitorDpiAwareness_();

      RegisterWindowClass_(ci.hInstance);

      DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
      if (ci.resizable)
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;

      DWORD exStyle = WS_EX_APPWINDOW;
      if (ci.acceptFileDrops)
        exStyle |= WS_EX_ACCEPTFILES;

      RECT r{ 0, 0, ci.clientWidth, ci.clientHeight };
      ::AdjustWindowRectEx(&r, style, FALSE, exStyle);

      const int winW = r.right - r.left;
      const int winH = r.bottom - r.top;

      HWND hwnd = ::CreateWindowExW(
        exStyle,
        kWindowClassName_,
        title_.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH,
        nullptr,
        nullptr,
        ci.hInstance,
        this // <-- used by WM_NCCREATE to bind HWND <-> Window*
      );

      if (!hwnd)
        throw std::runtime_error("CreateWindowExW failed.");

      // WM_NCCREATE sets hwnd_ (and GWLP_USERDATA). Keep width/height cached too.
      clientWidth_  = static_cast<std::uint32_t>(ci.clientWidth);
      clientHeight_ = static_cast<std::uint32_t>(ci.clientHeight);

      if (ci.startMaximized)
        ::ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
      else if (ci.visible)
        ::ShowWindow(hwnd_, SW_SHOWNORMAL);

      ::UpdateWindow(hwnd_);
    }

    void destroy()
    {
      if (hwnd_ != nullptr)
      {
        // DestroyWindow will synchronously send WM_DESTROY/WM_NCDESTROY.
        // WM_NCDESTROY will clear hwnd_.
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr; // extra safety
      }
    }

    // ---- Methods expected/probed by core/Application.cpp ----

    // Message pump (probe checks for pollMessages/pumpMessages/processMessages). :contentReference[oaicite:1]{index=1}
    void pollMessages() noexcept
    {
      MSG msg{};
      while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
      {
        if (msg.message == WM_QUIT)
        {
          shouldClose_ = true;
          exitCode_ = static_cast<int>(msg.wParam);
          break;
        }

        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
      }
    }

    bool shouldClose() const noexcept { return shouldClose_; }
    bool isOpen() const noexcept { return hwnd_ != nullptr && !shouldClose_; }

    // ---- Useful accessors for renderer/platform code ----

    HWND hwnd() const noexcept { return hwnd_; }
    HINSTANCE hinstance() const noexcept { return hinstance_; }

    std::uint32_t clientWidth()  const noexcept { return clientWidth_;  }
    std::uint32_t clientHeight() const noexcept { return clientHeight_; }

    bool isMinimized() const noexcept { return minimized_; }
    bool hasFocus()    const noexcept { return hasFocus_; }

    std::uint32_t dpi() const noexcept { return dpi_; }

    // Returns true once after a resize (useful for swapchain resize).
    bool consumeResize(std::uint32_t& outW, std::uint32_t& outH) noexcept
    {
      if (!resized_)
        return false;
      resized_ = false;
      outW = clientWidth_;
      outH = clientHeight_;
      return true;
    }

    int exitCode() const noexcept { return exitCode_; }

    void requestClose() noexcept
    {
      if (hwnd_)
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      else
        shouldClose_ = true;
    }

    void setTitle(std::wstring_view title)
    {
      title_ = std::wstring(title);
      if (hwnd_)
        ::SetWindowTextW(hwnd_, title_.c_str());
    }

  private:
    static constexpr const wchar_t* kWindowClassName_ = L"ColonyGameWindowClass";

    static void EnablePerMonitorDpiAwareness_() noexcept
    {
      // Best-effort. This is process-wide and safe to call multiple times.
      static bool done = false;
      if (done) return;
      done = true;

      // SetProcessDpiAwarenessContext is available on Win10+.
      using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
      HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
      if (!user32) return;

      auto fn = reinterpret_cast<Fn>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
      if (!fn) return;

      (void)fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    static void RegisterWindowClass_(HINSTANCE hInstance)
    {
      // Function-local statics are initialized once (thread-safe in C++11+).
      static ATOM atom = 0;
      if (atom != 0)
        return;

      WNDCLASSEXW wc{};
      wc.cbSize = sizeof(wc);
      wc.style = CS_HREDRAW | CS_VREDRAW;
      wc.lpfnWndProc = &Window::WndProc_;
      wc.hInstance = hInstance;
      wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
      wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
      wc.hIconSm = ::LoadIconW(nullptr, IDI_APPLICATION);
      wc.lpszClassName = kWindowClassName_;

      atom = ::RegisterClassExW(&wc);

      // If another TU already registered it, this can fail with class-exists.
      if (atom == 0)
      {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CLASS_ALREADY_EXISTS)
          atom = 1; // mark as "registered enough"
        else
          throw std::runtime_error("RegisterClassExW failed.");
      }
    }

    static LRESULT CALLBACK WndProc_(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
    {
      Window* self = nullptr;

      if (msg == WM_NCCREATE)
      {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);

        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        if (self)
          self->hwnd_ = hwnd;
      }
      else
      {
        self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      }

      LRESULT result = 0;
      if (self)
      {
        result = self->handleMessage_(hwnd, msg, wParam, lParam);

        if (msg == WM_NCDESTROY)
        {
          // Avoid dangling pointer in GWLP_USERDATA.
          ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
          self->hwnd_ = nullptr;
        }

        return result;
      }

      return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT handleMessage_(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
    {
      switch (msg)
      {
        case WM_CLOSE:
          shouldClose_ = true;
          ::DestroyWindow(hwnd);
          return 0;

        case WM_DESTROY:
          shouldClose_ = true;
          ::PostQuitMessage(0);
          return 0;

        case WM_SETFOCUS:
          hasFocus_ = true;
          return 0;

        case WM_KILLFOCUS:
          hasFocus_ = false;
          return 0;

        case WM_SIZE:
        {
          const UINT w = LOWORD(lParam);
          const UINT h = HIWORD(lParam);

          clientWidth_  = static_cast<std::uint32_t>(w);
          clientHeight_ = static_cast<std::uint32_t>(h);

          minimized_ = (wParam == SIZE_MINIMIZED);
          if (!minimized_)
            resized_ = true;

          return 0;
        }

        case WM_DPICHANGED:
        {
          dpi_ = static_cast<std::uint32_t>(HIWORD(wParam));

          // Recommended new window rect is provided in lParam
          const RECT* rc = reinterpret_cast<const RECT*>(lParam);
          if (rc)
          {
            ::SetWindowPos(
              hwnd,
              nullptr,
              rc->left,
              rc->top,
              rc->right - rc->left,
              rc->bottom - rc->top,
              SWP_NOZORDER | SWP_NOACTIVATE
            );
          }
          return 0;
        }

        default:
          return ::DefWindowProcW(hwnd, msg, wParam, lParam);
      }
    }

    void moveFrom(Window&& other) noexcept
    {
      hwnd_         = other.hwnd_;
      hinstance_    = other.hinstance_;
      clientWidth_  = other.clientWidth_;
      clientHeight_ = other.clientHeight_;
      dpi_          = other.dpi_;
      minimized_    = other.minimized_;
      hasFocus_     = other.hasFocus_;
      shouldClose_  = other.shouldClose_;
      resized_      = other.resized_;
      exitCode_     = other.exitCode_;
      title_        = std::move(other.title_);

      other.hwnd_ = nullptr;

      // Update GWLP_USERDATA so messages dispatch to the moved-to object.
      if (hwnd_)
        ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

  private:
    HWND      hwnd_      = nullptr;
    HINSTANCE hinstance_ = nullptr;

    std::uint32_t clientWidth_  = 0;
    std::uint32_t clientHeight_ = 0;
    std::uint32_t dpi_          = 96;

    bool minimized_   = false;
    bool hasFocus_    = true;
    bool shouldClose_ = false;
    bool resized_     = false;

    int exitCode_ = 0;

    std::wstring title_;
  };

} // namespace core
