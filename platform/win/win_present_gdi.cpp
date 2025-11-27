// platform/win/win_present_gdi.cpp
// Pure GDI present helpers — no dependency on engine types, only POD parameters.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif
#include <windows.h>
#include <math.h>

struct PresentConfig{ bool integerScale; bool smoothScale; };

static inline int clampi(int v, int lo, int hi){ return (v<lo)?lo:((v>hi)?hi:v); }

static RECT compute_dest_rect_(int cw,int ch,int bw,int bh,bool integerScale,float* outScale){
    float sx=(float)cw/(float)bw, sy=(float)ch/(float)bh;
    float scale = integerScale ? (float)clampi((int)floorf((sx<sy)?sx:sy),1,4096) : ((sx<sy)?sx:sy);
    if(scale<=0) scale=1.f;
    int dw=(int)floorf(bw*scale), dh=(int)floorf(bh*scale);
    int dx=(cw-dw)/2, dy=(ch-dh)/2;
    if(outScale) *outScale=scale;
    RECT r{dx,dy,dx+dw,dy+dh};
    return r;
}

// Paint letterbox/pillarbox bands around a destination rectangle.
static void paint_bands_(HDC hdc,int cw,int ch,const RECT& dst){
    int dstLeft = dst.left, dstTop = dst.top, dstRight = dst.right, dstBottom = dst.bottom;
    HBRUSH br=CreateSolidBrush(RGB(10,10,10));
    RECT r1,r2,r3,r4;
    SetRect(&r1, 0, 0, cw, dstTop);
    SetRect(&r2, 0, dstTop, dstLeft, dstBottom);
    SetRect(&r3, dstRight, dstTop, cw, dstBottom);
    SetRect(&r4, 0, dstBottom, cw, ch);
    FillRect(hdc,&r1,br); FillRect(hdc,&r2,br); FillRect(hdc,&r3,br); FillRect(hdc,&r4,br);
    DeleteObject(br);
}

void gdi_present_full(HWND hwnd, HDC hdc, int backW, int backH,
                      const void* pixels, const BITMAPINFO* bmi,
                      const PresentConfig& cfg)
{
    RECT cr; GetClientRect(hwnd,&cr); int cw=cr.right-cr.left, ch=cr.bottom-cr.top;
    float s=1.f; RECT dst=compute_dest_rect_(cw,ch,backW,backH,cfg.integerScale,&s);

    paint_bands_(hdc,cw,ch,dst);

    if(cfg.smoothScale && !cfg.integerScale){ SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc,0,0,nullptr); }
    else{ SetStretchBltMode(hdc, COLORONCOLOR); }

    int dx=dst.left, dy=dst.top, dw=dst.right-dst.left, dh=dst.bottom-dst.top;
    StretchDIBits(hdc, dx,dy,dw,dh, 0,0,backW,backH, pixels, bmi, DIB_RGB_COLORS, SRCCOPY);
}

void gdi_present_dirty(HWND hwnd, HDC hdc, int backW, int backH,
                       const void* pixels, const BITMAPINFO* bmi,
                       const RECT* rects, size_t numRects,
                       const PresentConfig& cfg)
{
    if(!rects || numRects==0){ gdi_present_full(hwnd,hdc,backW,backH,pixels,bmi,cfg); return; }

    RECT cr; GetClientRect(hwnd,&cr); int cw=cr.right-cr.left, ch=cr.bottom-cr.top;
    float s=1.f; RECT dst=compute_dest_rect_(cw,ch,backW,backH,cfg.integerScale,&s);

    paint_bands_(hdc,cw,ch,dst);

    if(cfg.smoothScale && !cfg.integerScale){ SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc,0,0,nullptr); }
    else{ SetStretchBltMode(hdc, COLORONCOLOR); }

    int dstLeft = dst.left, dstTop = dst.top;

    for(size_t i=0;i<numRects;i++){
        RECT sr = rects[i];
        int sw = (sr.right==INT32_MAX)?backW:(sr.right-sr.left);
        int sh = (sr.bottom==INT32_MAX)?backH:(sr.bottom-sr.top);
        if(sw<=0||sh<=0){ gdi_present_full(hwnd,hdc,backW,backH,pixels,bmi,cfg); return; }
        int ddx = dstLeft + (int)((float)sr.left * s);
        int ddy = dstTop  + (int)((float)sr.top  * s);
        int ddw = (int)floorf(sw*s), ddh=(int)floorf(sh*s);
        StretchDIBits(hdc, ddx,ddy,ddw,ddh, sr.left,sr.top,sw,sh, pixels, bmi, DIB_RGB_COLORS, SRCCOPY);
    }
}
