#pragma once
#include <windows.h>
#include <string>

namespace platform::win {

class WinWindow {
public:
    WinWindow();
    ~WinWindow();

    bool Create(const wchar_t* title, int width, int height);
    bool ProcessMessages();            // pump all pending, return false on WM_QUIT
    HWND GetHWND() const { return m_hwnd; }
    void GetClientSize(unsigned& w, unsigned& h) const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    HWND        m_hwnd = nullptr;
    HINSTANCE   m_hinst = nullptr;
    std::wstring m_className;
};

} // namespace platform::win
