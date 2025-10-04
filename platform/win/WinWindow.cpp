// Platform/Win/WinWindow.cpp (WndProc excerpt)
case WM_DPICHANGED: {
    // lParam points to a suggested RECT in the new DPI
    const RECT* const prcNewWindow = reinterpret_cast<RECT*>(lParam);
    SetWindowPos(hwnd, nullptr,
                 prcNewWindow->left, prcNewWindow->top,
                 prcNewWindow->right - prcNewWindow->left,
                 prcNewWindow->bottom - prcNewWindow->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // Recreate swapchain buffers here if you scale rendering with DPI
    return 0;
}
