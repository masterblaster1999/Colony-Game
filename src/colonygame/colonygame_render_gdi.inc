//======================================================================================
// GDI Rendering helpers (double buffer)
//======================================================================================
struct BackBuffer {
    HBITMAP bmp = 0;
    HBITMAP old = 0; // original selected object (for safe cleanup)
    HDC     mem = 0;
    int     w=0, h=0;
    void Create(HDC hdc, int W, int H) {
        Destroy();
        w=W; h=H;
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, W, H);
        old = (HBITMAP)SelectObject(mem, bmp); // keep old to reselect on Destroy
        HBRUSH b = CreateSolidBrush(RGB(0,0,0));
        RECT rc{0,0,W,H}; FillRect(mem, &rc, b); DeleteObject(b);
    }
    void Destroy() {
        if (mem) {
            if (old) { SelectObject(mem, old); old = 0; } // reselect original object first
            DeleteDC(mem); mem=0;
        }
        if (bmp) { DeleteObject(bmp); bmp=0; }
        w=h=0;
    }
};
