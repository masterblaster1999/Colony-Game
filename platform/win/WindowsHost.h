#pragma once
#include <windows.h>

class WindowsHost {
public:
    bool Create(HINSTANCE hInst, int nCmdShow);
    int  MessageLoop();
    HWND GetHwnd() const { return m_hwnd; }
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static WindowsHost* s_self;
    HWND m_hwnd = nullptr;
    void RegisterRawInput();   // KB + mouse
};
