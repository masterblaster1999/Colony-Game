// platform/win32_main.cpp (Ultra-Enhanced, Zero-Dependency)
// Windows SDK only: user32, gdi32, dwmapi, xinput9_1_0, winmm, shell32
// Features: MT software renderer (tiles), dirty-rect present, fixed/var timestep + pause/step/slowmo,
// record/replay, SIMD fills, alpha compositing, SDF shapes, soft shadows, dithering+gamma,
// screenshots (BMP) + clipboard, raw mouse + XInput, DPI-aware, borderless fullscreen,
// perf HUD + micro-profiler lanes + frame CRC32, hot-reloadable game.dll + PlatformAPI services,
// magnifier overlay, smooth vs crisp scaling toggle.  No third-party libs. No external assets.

// ---- Windows SDK hygiene ----------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif

#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM / GET_WHEEL_DELTA_WPARAM
#include <dwmapi.h>
#include <xinput.h>
#include <mmsystem.h>   // TIMECAPS, timeGetDevCaps, timeBeginPeriod, timeEndPeriod
#include <shellapi.h>

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <string.h>

#if defined(_M_AMD64) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__)
#  define COLONY_HAS_SSE2 1
#  include <emmintrin.h> // SSE2
#else
#  define COLONY_HAS_SSE2 0
#endif

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "xinput9_1_0.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

// Fallbacks in case older headers/toolchains don't define these macros
#ifndef GET_X_LPARAM
#  define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#  define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif
#ifndef GET_WHEEL_DELTA_WPARAM
#  define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#endif

// --------------------------------------------------------
// Utils
// --------------------------------------------------------
static inline int  clampi(int v, int lo, int hi){ return (v<lo)?lo:((v>hi)?hi:v); }
static inline float clampf(float v, float lo, float hi){ return (v<lo)?lo:((v>hi)?hi:v); }
static inline uint32_t rgb8(uint8_t r,uint8_t g,uint8_t b){ return (uint32_t)b<<16 | (uint32_t)g<<8 | (uint32_t)r; } // 0x00BBGGRR
static inline uint32_t rgba8(uint8_t r,uint8_t g,uint8_t b,uint8_t a){ return (uint32_t)a<<24 | rgb8(r,g,b); }
static inline uint64_t now_qpc(){ LARGE_INTEGER li; QueryPerformanceCounter(&li); return (uint64_t)li.QuadPart; }
static inline double   qpc_to_sec(uint64_t t){ static double inv=0; if(!inv){ LARGE_INTEGER f; QueryPerformanceFrequency(&f); inv=1.0/(double)f.QuadPart; } return t*inv; }
static inline uint32_t hash32(uint32_t x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b){ return (uint32_t)b<<16 | (uint32_t)g<<8 | (uint32_t)r; }

// --------------------------------------------------------
// Backbuffer
// --------------------------------------------------------
struct Backbuffer{
    int w=0,h=0,pitch=0;
    void* pixels=nullptr; // 32bpp 0x00BBGGRR, top-down
    BITMAPINFO bmi{};
    void alloc(int W,int H){
        free();
        w=(W>0?W:1); h=(H>0?H:1); pitch=w*4;
        size_t sz=(size_t)w*(size_t)h*4;
        pixels=VirtualAlloc(nullptr, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        bmi={};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=w;
        bmi.bmiHeader.biHeight=-h; // top-down
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
    }
    void free(){ if(pixels){ VirtualFree(pixels,0,MEM_RELEASE); pixels=nullptr; } w=h=pitch=0; }
};
static inline void* rowptr(Backbuffer& bb, int y){ return (uint8_t*)bb.pixels + (size_t)y*bb.pitch; }

// --------------------------------------------------------
// Input
// --------------------------------------------------------
struct Button{ bool down=false; uint8_t trans=0; };
static inline void set_button(Button& b, bool d){ if(b.down!=d){ b.down=d; b.trans++; } }
static inline void begin_frame(Button& b){ b.trans=0; }
static inline bool pressed(const Button& b){ return b.down && b.trans>0; }

enum KeyCode{
    Key_Unknown=0,
    Key_W,Key_A,Key_S,Key_D, Key_Q,Key_E,
    Key_Space, Key_Escape, Key_Up,Key_Down,Key_Left,Key_Right,
    Key_F1,Key_F2,Key_F3,Key_F4,Key_F5,Key_F6,Key_F7,Key_F8,Key_F9,Key_F10,Key_F11,Key_F12,
    Key_Z, Key_H, Key_G,
    Key_Count
};

struct Gamepad{
    bool connected=false; float lx=0,ly=0,rx=0,ry=0, lt=0,rt=0;
    Button a,b,x,y, lb,rb, back,start, lsb,rsb, up,down,left,right;
};

struct InputState{
    int mouseX=0,mouseY=0,mouseDX=0,mouseDY=0;
    float wheel=0.f;
    Button mouseL,mouseM,mouseR;
    Button key[Key_Count]{};
    Gamepad pads[4]{};
    bool rawMouse=false;
    char text[128]{}; int textLen=0;
};

static inline void begin_frame(InputState& in){
    in.wheel=0; in.mouseDX=in.mouseDY=0; in.textLen=0; in.text[0]=0;
    begin_frame(in.mouseL); begin_frame(in.mouseM); begin_frame(in.mouseR);
    for(int i=0;i<Key_Count;i++) begin_frame(in.key[i]);
}

// --------------------------------------------------------
// Tiny 6x8 bitmap font (ASCII 32..127). Missing glyphs render blank.
// --------------------------------------------------------
#define GLYPH6x8(...) {__VA_ARGS__}
static const uint8_t kFont6x8[96][8] = {
    GLYPH6x8(0,0,0,0,0,0,0,0), // ' '
    GLYPH6x8(0x30,0x30,0x30,0x30,0x30,0,0x30,0), // '!'
    GLYPH6x8(0x6c,0x6c,0x48,0,0,0,0,0), // '"'
    GLYPH6x8(0x6c,0xfe,0x6c,0x6c,0xfe,0x6c,0,0), // '#'
    GLYPH6x8(0x10,0x7c,0x90,0x78,0x14,0xf8,0x10,0), // '$'
    GLYPH6x8(0xc4,0xc8,0x10,0x20,0x46,0x86,0,0), // '%'
    GLYPH6x8(0x30,0x48,0x30,0x52,0x8c,0xcc,0x76,0), // '&'
    GLYPH6x8(0x30,0x30,0x20,0,0,0,0,0), // '''
    GLYPH6x8(0x18,0x30,0x60,0x60,0x60,0x30,0x18,0), // '('
    GLYPH6x8(0x60,0x30,0x18,0x18,0x18,0x30,0x60,0), // ')'
    GLYPH6x8(0,0x44,0x38,0xfe,0x38,0x44,0,0), // '*'
    GLYPH6x8(0,0x10,0x10,0x7c,0x10,0x10,0,0), // '+'
    GLYPH6x8(0,0,0,0,0,0x30,0x30,0x20), // ','
    GLYPH6x8(0,0,0,0x7c,0,0,0,0), // '-'
    GLYPH6x8(0,0,0,0,0,0x30,0x30,0), // '.'
    GLYPH6x8(0x04,0x08,0x10,0x20,0x40,0x80,0,0), // '/'
    GLYPH6x8(0x7c,0x82,0x8a,0x92,0xa2,0x82,0x7c,0), // '0'
    GLYPH6x8(0x10,0x30,0x10,0x10,0x10,0x10,0x7c,0), // '1'
    GLYPH6x8(0x7c,0x82,0x04,0x18,0x60,0x80,0xfe,0), // '2'
    GLYPH6x8(0x7c,0x82,0x04,0x38,0x04,0x82,0x7c,0), // '3'
    GLYPH6x8(0x08,0x18,0x28,0x48,0x88,0xfe,0x08,0), // '4'
    GLYPH6x8(0xfe,0x80,0x80,0xfc,0x02,0x02,0x82,0x7c,0), // '5'
    GLYPH6x8(0x3c,0x40,0x80,0xfc,0x82,0x82,0x7c,0), // '6'
    GLYPH6x8(0xfe,0x82,0x04,0x08,0x10,0x10,0x10,0), // '7'
    GLYPH6x8(0x7c,0x82,0x82,0x7c,0x82,0x82,0x7c,0), // '8'
    GLYPH6x8(0x7c,0x82,0x82,0x7e,0x02,0x04,0x78,0), // '9'
    GLYPH6x8(0,0x30,0x30,0,0x30,0x30,0,0), // ':'
    GLYPH6x8(0,0x30,0x30,0,0x30,0x30,0x20,0), // ';'
    GLYPH6x8(0x0c,0x30,0xc0,0x30,0x0c,0,0,0), // '<'
    GLYPH6x8(0,0x7c,0,0x7c,0,0,0,0), // '='
    GLYPH6x8(0xc0,0x30,0x0c,0x30,0xc0,0,0,0), // '>'
    GLYPH6x8(0x7c,0x82,0x04,0x18,0x10,0,0x10,0), // '?'
    GLYPH6x8(0x7c,0x82,0xba,0xaa,0xbe,0x80,0x7c,0), // '@'
    GLYPH6x8(0x38,0x44,0x82,0xfe,0x82,0x82,0x82,0), // 'A'
    GLYPH6x8(0xfc,0x82,0x82,0xfc,0x82,0x82,0xfc,0), // 'B'
    GLYPH6x8(0x7c,0x82,0x80,0x80,0x80,0x82,0x7c,0), // 'C'
    GLYPH6x8(0xf8,0x84,0x82,0x82,0x82,0x84,0xf8,0), // 'D'
    GLYPH6x8(0xfe,0x80,0x80,0xfc,0x80,0x80,0xfe,0), // 'E'
    GLYPH6x8(0xfe,0x80,0x80,0xfc,0x80,0x80,0x80,0), // 'F'
    GLYPH6x8(0x7c,0x82,0x80,0x8e,0x82,0x82,0x7e,0), // 'G'
    GLYPH6x8(0x82,0x82,0x82,0xfe,0x82,0x82,0x82,0), // 'H'
    GLYPH6x8(0x7c,0x10,0x10,0x10,0x10,0x10,0x7c,0), // 'I'
    GLYPH6x8(0x3e,0x04,0x04,0x04,0x84,0x84,0x78,0), // 'J'
    GLYPH6x8(0x82,0x84,0x88,0xf0,0x88,0x84,0x82,0), // 'K'
    GLYPH6x8(0x80,0x80,0x80,0x80,0x80,0x80,0xfe,0), // 'L'
    GLYPH6x8(0x82,0xc6,0xaa,0x92,0x82,0x82,0x82,0), // 'M'
    GLYPH6x8(0x82,0xc2,0xa2,0x92,0x8a,0x86,0x82,0), // 'N'
    GLYPH6x8(0x7c,0x82,0x82,0x82,0x82,0x82,0x7c,0), // 'O'
    GLYPH6x8(0xfc,0x82,0x82,0xfc,0x80,0x80,0x80,0), // 'P'
    GLYPH6x8(0x7c,0x82,0x82,0x82,0x92,0x8c,0x7e,0), // 'Q'
    GLYPH6x8(0xfc,0x82,0x82,0xfc,0x88,0x84,0x82,0), // 'R'
    GLYPH6x8(0x7c,0x80,0x7c,0x02,0x02,0x82,0x7c,0), // 'S'
    GLYPH6x8(0xfe,0x10,0x10,0x10,0x10,0x10,0x10,0), // 'T'
    GLYPH6x8(0x82,0x82,0x82,0x82,0x82,0x82,0x7c,0), // 'U'
    GLYPH6x8(0x82,0x82,0x44,0x44,0x28,0x28,0x10,0), // 'V'
    GLYPH6x8(0x82,0x92,0xaa,0xc6,0x82,0x82,0x82,0), // 'W'
    GLYPH6x8(0x82,0x44,0x28,0x10,0x28,0x44,0x82,0), // 'X'
    GLYPH6x8(0x82,0x44,0x28,0x10,0x10,0x10,0x10,0), // 'Y'
    GLYPH6x8(0xfe,0x04,0x08,0x30,0x40,0x80,0xfe,0), // 'Z'
    // (remaining glyphs default to zero)
};
#undef GLYPH6x8

static void draw_char6x8(Backbuffer& bb, int x, int y, char ch, uint32_t color){
    if(ch<32 || ch>127) ch='?';
    const uint8_t* g = kFont6x8[ch-32];
    if(y<0||y+8>bb.h) return;
    for(int row=0; row<8; ++row){
        uint8_t bits=g[row];
        if(bits==0) continue;
        int ry = y + row;
        if(ry<0 || ry>=bb.h) continue;
        uint32_t* dstRow = (uint32_t*)rowptr(bb, ry);
        for(int col=0; col<6; ++col){
            if((bits>>col)&1){
                int xx = x + col;
                if(xx>=0 && xx<bb.w) dstRow[xx] = color; // safe: no negative-index pointer
            }
        }
    }
}
static void draw_text6x8(Backbuffer& bb,int x,int y,const char* s,uint32_t c){ for(;*s;++s,x+=6) draw_char6x8(bb,x,y,*s,c); }

// --------------------------------------------------------
// Procedural basics + dithering
// --------------------------------------------------------
static void fill_rect_scalar(Backbuffer& bb,int x,int y,int w,int h,uint32_t c){
    int x0=clampi(x,0,bb.w), y0=clampi(y,0,bb.h);
    int x1=clampi(x+w,0,bb.w), y1=clampi(y+h,0,bb.h);
    for(int yy=y0; yy<y1; ++yy){
        uint32_t* row=(uint32_t*)rowptr(bb,yy);
        for(int xx=x0; xx<x1; ++xx) row[xx]=c;
    }
}
static void line(Backbuffer& bb,int x0,int y0,int x1,int y1,uint32_t c){
    int dx=abs(x1-x0), sx=x0<x1?1:-1;
    int dy=-abs(y1-y0), sy=y0<y1?1:-1;
    int err=dx+dy;
    for(;;){
        if((unsigned)x0<(unsigned)bb.w && (unsigned)y0<(unsigned)bb.h) ((uint32_t*)rowptr(bb,y0))[x0]=c;
        if(x0==x1 && y0==y1) break;
        int e2=2*err; if(e2>=dy){err+=dy; x0+=sx;} if(e2<=dx){err+=dx; y0+=sy;}
    }
}
static inline uint32_t tile_color(int tx,int ty){
    uint32_t h = hash32((uint32_t)tx*73856093u ^ (uint32_t)ty*19349663u);
    uint8_t r=(uint8_t)(128+(h&63)), g=(uint8_t)(80+((h>>8)&127)), b=(uint8_t)(80+((h>>16)&127));
    return rgb8(r,g,b);
}

static const uint8_t kBayer8[8][8] = {
    { 0,48,12,60, 3,51,15,63}, {32,16,44,28,35,19,47,31},
    { 8,56, 4,52,11,59, 7,55}, {40,24,36,20,43,27,39,23},
    { 2,50,14,62, 1,49,13,61}, {34,18,46,30,33,17,45,29},
    {10,58, 6,54, 9,57, 5,53}, {42,26,38,22,41,25,37,21}
};
static void apply_dither_gamma(Backbuffer& bb, bool gamma){
    for(int y=0;y<bb.h;y++){
        uint32_t* row=(uint32_t*)rowptr(bb,y);
        for(int x=0;x<bb.w;x++){
            uint32_t p=row[x];
            uint8_t r=(uint8_t)(p&0xFF), g=(uint8_t)((p>>8)&0xFF), b=(uint8_t)((p>>16)&0xFF);
            uint8_t t=kBayer8[y&7][x&7];
            auto d=[&](uint8_t v)->uint8_t{
                int vv = v + (int)t - 31; vv=clampi(vv,0,255);
                if(gamma){ float f=(float)vv/255.f; f=powf(f, 1.0f/2.0f); vv=(int)(f*255.f+0.5f); }
                return (uint8_t)vv;
            };
            row[x] = rgb8(d(r),d(g),d(b));
        }
    }
}

// --------------------------------------------------------
// SIMD fills + compositing + SDF + soft shadows
// --------------------------------------------------------
#if COLONY_HAS_SSE2
static void clear_solid_sse2(Backbuffer& bb, uint32_t c){
    __m128i v=_mm_set1_epi32((int)c);
    for(int y=0;y<bb.h;y++){
        __m128i* p=(__m128i*)rowptr(bb,y);
        int n=bb.w; int i=0;
        for(;i<=n-4;i+=4) _mm_storeu_si128(p++, v);
        uint32_t* tail=(uint32_t*)p; for(;i<n;i++) *tail++=c;
    }
}
static void fill_rect_sse2(Backbuffer& bb,int x,int y,int w,int h,uint32_t c){
    int x0=clampi(x,0,bb.w), y0=clampi(y,0,bb.h);
    int x1=clampi(x+w,0,bb.w), y1=clampi(y+h,0,bb.h);
    if(x0>=x1||y0>=y1) return;
    __m128i v=_mm_set1_epi32((int)c);
    int span=x1-x0; int vecN=span&~3;
    for(int yy=y0; yy<y1; ++yy){
        uint32_t* row=(uint32_t*)rowptr(bb,yy)+x0;
        __m128i* pv=(__m128i*)row;
        for(int i=0;i<vecN;i+=4) _mm_storeu_si128(pv++, v);
        for(int i=vecN;i<span;++i) row[i]=c;
    }
}
#else
// Scalar fallbacks for toolchains without SSE2
static void clear_solid_sse2(Backbuffer& bb, uint32_t c){
    for(int y=0;y<bb.h;y++){
        uint32_t* p=(uint32_t*)rowptr(bb,y);
        for(int x=0;x<bb.w;x++) p[x]=c;
    }
}
static void fill_rect_sse2(Backbuffer& bb,int x,int y,int w,int h,uint32_t c){
    fill_rect_scalar(bb,x,y,w,h,c);
}
#endif

static inline uint32_t alpha_over(uint32_t dst, uint32_t srcARGB){
    int a=(int)((srcARGB>>24)&0xFF); if(a<=0) return dst; int inv=255-a;
    int db=(dst>>16)&0xFF, dg=(dst>>8)&0xFF, dr=(dst)&0xFF;
    int sr=(srcARGB>>16)&0xFF, sg=(srcARGB>>8)&0xFF, sb=(srcARGB)&0xFF;
    int r=((sr*a+127)/255) + ((dr*inv+127)/255);
    int g=((sg*a+127)/255) + ((dg*inv+127)/255);
    int b=((sb*a+127)/255) + ((db*inv+127)/255);
    return (uint32_t)((b<<16)|(g<<8)|r);
}
static inline uint8_t aa_from_distance(float d){
    float a=0.5f - d; if(a<=0) return 0; if(a>=1) return 255; return (uint8_t)(a*255.f+0.5f);
}
static void draw_sdf_circle(Backbuffer& bb,float cx,float cy,float r,uint32_t rgb,float borderPx=1.0f){
    // Removed unused minX to avoid C4189 under /WX
    int maxX=clampi((int)ceilf(cx+r+borderPx),0,bb.w);
    int minY=clampi((int)floorf(cy-r-borderPx),0,bb.h), maxY=clampi((int)ceilf(cy+r+borderPx),0,bb.h);
    for(int y=minY; y<maxY; ++y){
        uint32_t* rowp=(uint32_t*)rowptr(bb,y);
        float fy=(float)y+0.5f;
        for(int x=0; x<bb.w && x<maxX; ++x){
            float fx=(float)x+0.5f;
            float d=sqrtf((fx-cx)*(fx-cx)+(fy-cy)*(fy-cy))-r;
            uint8_t a=aa_from_distance(d/borderPx); if(!a) continue;
            uint32_t src=(uint32_t(a)<<24)|((rgb>>16)&0xFF)<<16|((rgb>>8)&0xFF)<<8|(rgb&0xFF);
            rowp[x]=alpha_over(rowp[x],src);
        }
    }
}
static void draw_sdf_roundrect(Backbuffer& bb,float x,float y,float w,float h,float r,uint32_t rgb,float borderPx=1.0f){
    float x2=x+w, y2=y+h;
    int minX=clampi((int)floorf(x-r-borderPx),0,bb.w), maxX=clampi((int)ceilf(x2+r+borderPx),0,bb.w);
    int minY=clampi((int)floorf(y-r-borderPx),0,bb.h), maxY=clampi((int)ceilf(y2+r+borderPx),0,bb.h);
    for(int iy=minY; iy<maxY; ++iy){
        uint32_t* row=(uint32_t*)rowptr(bb,iy);
        float py=(float)iy+0.5f;
        for(int ix=minX; ix<maxX; ++ix){
            float px=(float)ix+0.5f;
            float dx=fmaxf(fabsf(px-(x+w*0.5f))-(w*0.5f-r),0.0f);
            float dy=fmaxf(fabsf(py-(y+h*0.5f))-(h*0.5f-r),0.0f);
            float d=sqrtf(dx*dx+dy*dy)-r;
            uint8_t a=aa_from_distance(d/borderPx); if(!a) continue;
            uint32_t src=(uint32_t(a)<<24)|((rgb>>16)&0xFF)<<16|((rgb>>8)&0xFF)<<8|(rgb&0xFF);
            row[ix]=alpha_over(row[ix],src);
        }
    }
}
static void blur_box_horizontal(uint8_t* a,int w,int h,int r){
    if(r<=0) return; std::vector<uint8_t> tmp(w*h);
    for(int y=0;y<h;y++){
        int acc=0; int norm=r*2+1;
        for(int i=-r;i<=r;i++){ int x=clampi(i,0,w-1); acc+=a[y*w+x]; }
        for(int x=0;x<w;x++){
            tmp[y*w+x]=(uint8_t)(acc/norm);
            int x_add=clampi(x+r+1,0,w-1), x_sub=clampi(x-r,0,w-1);
            acc += a[y*w+x_add] - a[y*w+x_sub];
        }
    }
    memcpy(a,tmp.data(),(size_t)w*h);
}
static void blur_box_vertical(uint8_t* a,int w,int h,int r){
    if(r<=0) return; std::vector<uint8_t> tmp(w*h);
    for(int x=0;x<w;x++){
        int acc=0; int norm=r*2+1;
        for(int i=-r;i<=r;i++){ int y=clampi(i,0,h-1); acc+=a[y*w+x]; }
        for(int y=0;y<h;y++){
            tmp[y*w+x]=(uint8_t)(acc/norm);
            int y_add=clampi(y+r+1,0,h-1), y_sub=clampi(y-r,0,h-1);
            acc += a[y_add*w+x] - a[y_sub*w+x];
        }
    }
    memcpy(a,tmp.data(),(size_t)w*h);
}
static void draw_soft_shadow(Backbuffer& bb,int x,int y,int w,int h,int radiusPx,uint8_t alpha,uint32_t rgb){
    if(w<=0||h<=0) return;
    std::vector<uint8_t> mask((size_t)w*h, alpha);
    blur_box_horizontal(mask.data(),w,h,radiusPx);
    blur_box_vertical  (mask.data(),w,h,radiusPx);
    int x0=clampi(x,0,bb.w), y0=clampi(y,0,bb.h);
    int x1=clampi(x+w,0,bb.w), y1=clampi(y+h,0,bb.h);
    for(int yy=y0; yy<y1; ++yy){
        uint32_t* dst=(uint32_t*)rowptr(bb,yy);
        int my=yy-y;
        for(int xx=x0; xx<x1; ++xx){
            int mx=xx-x; uint8_t a=mask[(size_t)my*w + mx]; if(!a) continue;
            uint32_t src=(uint32_t(a)<<24)|((rgb>>16)&0xFF)<<16|((rgb>>8)&0xFF)<<8|(rgb&0xFF);
            dst[xx]=alpha_over(dst[xx],src);
        }
    }
}

// --------------------------------------------------------
// Thread pool (tile jobs)
// --------------------------------------------------------
struct TileJob{ int y0,y1; void(*fn)(void*,int,int); void* ctx; };
class ThreadPool{
public:
    void init(int threads){ shutdown(); if(threads<1) threads=1; stop=false; for(int i=0;i<threads;i++) workers.emplace_back([this]{ worker(); }); }
    void shutdown(){ { std::lock_guard<std::mutex> lk(mx); stop=true; } cv.notify_all(); for(auto& t:workers){ if(t.joinable()) t.join(); } workers.clear(); }
    void dispatch(const std::vector<TileJob>& jobs){ std::lock_guard<std::mutex> lk(mx); queue=jobs; next=0; pending=(int)queue.size(); cv.notify_all(); }
    void wait(){ std::unique_lock<std::mutex> lk(mx); doneCv.wait(lk,[&]{return pending==0;}); }
    ~ThreadPool(){ shutdown(); }
private:
    void worker(){
        for(;;){
            // value-initialize to avoid use-before-init warnings on older toolsets
            TileJob job{}; bool has=false;
            {
                std::unique_lock<std::mutex> lk(mx);
                cv.wait(lk,[&]{return stop || next<(int)queue.size();});
                if(stop) return;
                if(next<(int)queue.size()){ job=queue[next++]; has=true; }
            }
            if(has){
                job.fn(job.ctx, job.y0, job.y1);
                if(--pending==0){ std::lock_guard<std::mutex> lk(mx); doneCv.notify_all(); }
            }
        }
    }
    std::vector<std::thread> workers; std::vector<TileJob> queue; int next=0; std::atomic<int> pending{0};
    std::mutex mx; std::condition_variable cv, doneCv; bool stop=false;
};

// --------------------------------------------------------
// Dirty rectangles
// --------------------------------------------------------
struct Dirty{ RECT r; };
struct DirtyTracker{
    std::vector<Dirty> rects;
    void clear(){ rects.clear(); }
    void mark(int x,int y,int w,int h){
        if(w<=0||h<=0) return;
        RECT r{ x,y,x+w,y+h };
        rects.push_back({r});
        if(rects.size()>256){ rects.clear(); rects.push_back({ RECT{0,0,INT32_MAX,INT32_MAX} }); }
    }
};

// --------------------------------------------------------
// Platform/Game APIs + hot reload
// --------------------------------------------------------
struct PlatformAPI{
    void (*log_text)(const char* msg)=nullptr;
    double (*time_now_sec)()=nullptr;
    bool (*screenshot_bmp)(const char* path)=nullptr;
    bool (*clipboard_copy_bitmap)()=nullptr;
    bool (*file_write_all)(const char* path, const void* data, size_t bytes)=nullptr;
    bool (*file_read_all)(const char* path, std::vector<uint8_t>* out)=nullptr;
};
struct InputState; // fwd
struct GameAPI{
    void (*init)(void** user, int w, int h)=nullptr;
    void (*resize)(void* user, int w, int h)=nullptr;
    // Legacy combined path (fallback)
    void (*update_and_render)(void* user, float dt, uint32_t* pixels, int w, int h, const InputState* input)=nullptr;
    void (*bind_platform)(PlatformAPI* plat, int version)=nullptr;
    // New optional decoupled path (preferred for fixed-step)
    void (*update_fixed)(void* user, float dt)=nullptr; // simulate only
    void (*render)(void* user, float alpha, uint32_t* pixels, int w, int h, const InputState* input)=nullptr; // render once with interpolation alpha
};
struct HotReload{
    HMODULE dll=nullptr; FILETIME lastWrite{}; GameAPI api{}; void* userState=nullptr; bool active=false;
};
static FILETIME filetimeA(const char* path){ WIN32_FILE_ATTRIBUTE_DATA d{}; if(GetFileAttributesExA(path,GetFileExInfoStandard,&d)) return d.ftLastWriteTime; FILETIME z{}; return z; }
static bool file_existsA(const char* path){ DWORD a=GetFileAttributesA(path); return (a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY)); }
static bool load_game(HotReload& hr, const char* dllName){
    char tmp[MAX_PATH]; wsprintfA(tmp,"%s_hot.dll",dllName); CopyFileA(dllName,tmp,FALSE);
    HMODULE dll=LoadLibraryA(tmp); if(!dll) return false;
    auto init=(void(*)(void**,int,int))GetProcAddress(dll,"game_init");
    auto resize=(void(*)(void*,int,int))GetProcAddress(dll,"game_resize");
    auto step=(void(*)(void*,float,uint32_t*,int,int,const InputState*))GetProcAddress(dll,"game_update_and_render");
    auto bind=(void(*)(PlatformAPI*,int))GetProcAddress(dll,"game_bind_platform");
    // New optional decoupled entry points
    auto upf =(void(*)(void*,float))GetProcAddress(dll,"game_update_fixed");
    auto rend=(void(*)(void*,float,uint32_t*,int,int,const InputState*))GetProcAddress(dll,"game_render");

    if(!step && !upf && !rend){ FreeLibrary(dll); DeleteFileA(tmp); return false; }
    hr.dll=dll; hr.api={};
    hr.api.init=init; hr.api.resize=resize; hr.api.update_and_render=step; hr.api.bind_platform=bind;
    hr.api.update_fixed=upf; hr.api.render=rend;
    hr.active=true; return true;
}
static void unload_game(HotReload& hr){ if(hr.dll){ FreeLibrary(hr.dll); hr.dll=nullptr; } hr.api={}; hr.userState=nullptr; hr.active=false; }

// --------------------------------------------------------
// Window state + DPI + present
// --------------------------------------------------------
struct WindowState{
    HWND hwnd=nullptr; bool running=true;
    bool useVsync=true; bool integerScale=true; bool borderless=false; bool enableRawMouse=true;
    bool fixedTimestep=false; float fixedDT=1.f/60.f;
    bool smoothScale=false; // HALFTONE when not integer scaling
    int baseW=1280, baseH=720; UINT dpi=96;
} g_win;

static void set_dpi_awareness(){
    HMODULE user=GetModuleHandleA("user32.dll");
    using SetDpiCtx=BOOL(WINAPI*)(HANDLE);
    if(user){ auto p=(SetDpiCtx)GetProcAddress(user,"SetProcessDpiAwarenessContext");
        if(p){ p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); return; } }
    SetProcessDPIAware();
}
static void toggle_fullscreen(HWND hwnd){
    static WINDOWPLACEMENT prev={sizeof(prev)};
    DWORD style=(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE);
    if(!g_win.borderless){
        MONITORINFO mi={sizeof(mi)}; GetWindowPlacement(hwnd,&prev);
        GetMonitorInfo(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
        SetWindowLongPtr(hwnd,GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd,HWND_TOP, mi.rcMonitor.left,mi.rcMonitor.top,
            mi.rcMonitor.right-mi.rcMonitor.left, mi.rcMonitor.bottom-mi.rcMonitor.top,
            SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        g_win.borderless=true;
    }else{
        SetWindowLongPtr(hwnd,GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd,&prev);
        SetWindowPos(hwnd,nullptr,0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        g_win.borderless=false;
    }
}
static RECT compute_dest_rect(int cw,int ch,int bw,int bh,float* outScale=nullptr){
    float sx=(float)cw/(float)bw, sy=(float)ch/(float)bh;
    float scale = g_win.integerScale ? (float)clampi((int)floorf(fminf(sx,sy)),1,4096) : fminf(sx,sy);
    if(scale<=0) scale=1.f;
    int dw=(int)floorf(bw*scale), dh=(int)floorf(bh*scale), dx=(cw-dw)/2, dy=(ch-dh)/2;
    if(outScale) *outScale=scale; return RECT{dx,dy,dx+dw,dy+dh};
}
static void present_full(HWND hwnd,HDC hdc,Backbuffer& bb){
    RECT cr; GetClientRect(hwnd,&cr); int cw=cr.right-cr.left, ch=cr.bottom-cr.top; float s=1.f; RECT dst=compute_dest_rect(cw,ch,bb.w,bb.h,&s);
    HBRUSH br=CreateSolidBrush(RGB(10,10,10));
    RECT r1={0,0,cw,dst.top}, r2={0,dst.top,dst.left,dst.bottom}, r3={dst.right,dst.top,cw,dst.bottom}, r4={0,dst.bottom,cw,ch};
    FillRect(hdc,&r1,br); FillRect(hdc,&r2,br); FillRect(hdc,&r3,br); FillRect(hdc,&r4,br);  // ensure all bands are painted
    DeleteObject(br);
    if(g_win.smoothScale && !g_win.integerScale){ SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc,0,0,nullptr); }
    else{ SetStretchBltMode(hdc, COLORONCOLOR); }
    int dx=dst.left, dy=dst.top, dw=dst.right-dst.left, dh=dst.bottom-dst.top;
    StretchDIBits(hdc, dx,dy,dw,dh, 0,0,bb.w,bb.h, bb.pixels, &bb.bmi, DIB_RGB_COLORS, SRCCOPY);
}
static void present_dirty(HWND hwnd,HDC hdc,Backbuffer& bb,const DirtyTracker& dirty){
    RECT cr; GetClientRect(hwnd,&cr); int cw=cr.right-cr.left, ch=cr.bottom-cr.top; float s=1.f; RECT dst=compute_dest_rect(cw,ch,bb.w,bb.h,&s);
    if(dirty.rects.empty()){ present_full(hwnd,hdc,bb); return; } // early out before painting background
    HBRUSH br=CreateSolidBrush(RGB(10,10,10));
    RECT r1={0,0,cw,dst.top}, r2={0,dst.top,dst.left,dst.bottom}, r3={dst.right,dst.top,cw,dst.bottom}, r4={0,dst.bottom,cw,ch};
    FillRect(hdc,&r1,br); FillRect(hdc,&r2,br); FillRect(hdc,&r3,br); FillRect(hdc,&r4,br);
    DeleteObject(br);
    if(g_win.smoothScale && !g_win.integerScale){ SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc,0,0,nullptr); }
    else{ SetStretchBltMode(hdc, COLORONCOLOR); }
    for(const auto& d: dirty.rects){
        RECT srect=d.r;
        int sw=(srect.right==INT32_MAX)?bb.w:(srect.right-srect.left);
        int sh=(srect.bottom==INT32_MAX)?bb.h:(srect.bottom-srect.top);
        if(sw<=0||sh<=0){ present_full(hwnd,hdc,bb); return; }
        int ddx=dst.left + (int)((float)srect.left * s);
        int ddy=dst.top  + (int)((float)srect.top  * s);
        int ddw=(int)floorf(sw*s), ddh=(int)floorf(sh*s);
        StretchDIBits(hdc, ddx,ddy,ddw,ddh, srect.left,srect.top,sw,sh, bb.pixels,&bb.bmi,DIB_RGB_COLORS,SRCCOPY);
    }
}

// --------------------------------------------------------
// Global state
// --------------------------------------------------------
static Backbuffer g_bb;
static InputState g_in;
static ThreadPool g_pool;
static DirtyTracker g_dirty;
static UINT g_timerPeriod = 0; // tracks the period actually begun, to correctly end it

// --------------------------------------------------------
// XInput
// --------------------------------------------------------
static float norm_stick(SHORT v){ const float inv=1.0f/32767.0f; float f=(float)v*inv; return clampf(f,-1.f,1.f); }
static float norm_trig(BYTE v){ const float inv=1.0f/255.0f; return (float)v*inv; }
static void poll_gamepads(InputState& in){
    for(DWORD i=0;i<4;i++){
        XINPUT_STATE st{}; DWORD r=XInputGetState(i,&st);
        Gamepad& p=in.pads[i]; p.connected=(r==ERROR_SUCCESS); if(!p.connected) continue;
        const XINPUT_GAMEPAD& g=st.Gamepad;
        auto set=[&](Button& b,bool d){ set_button(b,d); };
        p.lx=norm_stick(g.sThumbLX); p.ly=norm_stick(g.sThumbLY);
        p.rx=norm_stick(g.sThumbRX); p.ry=norm_stick(g.sThumbRY);
        p.lt=norm_trig(g.bLeftTrigger); p.rt=norm_trig(g.bRightTrigger);
        set(p.a, g.wButtons&XINPUT_GAMEPAD_A); set(p.b, g.wButtons&XINPUT_GAMEPAD_B);
        set(p.x, g.wButtons&XINPUT_GAMEPAD_X); set(p.y, g.wButtons&XINPUT_GAMEPAD_Y);
        set(p.lb,g.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER); set(p.rb,g.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
        set(p.back,g.wButtons&XINPUT_GAMEPAD_BACK); set(p.start,g.wButtons&XINPUT_GAMEPAD_START);
        set(p.lsb,g.wButtons&XINPUT_GAMEPAD_LEFT_THUMB); set(p.rsb,g.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB);
        set(p.up,g.wButtons&XINPUT_GAMEPAD_DPAD_UP); set(p.down,g.wButtons&XINPUT_GAMEPAD_DPAD_DOWN);
        set(p.left,g.wButtons&XINPUT_GAMEPAD_DPAD_LEFT); set(p.right,g.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT);
    }
}

// --------------------------------------------------------
// Raw mouse
// --------------------------------------------------------
static void enable_raw_mouse(HWND hwnd,bool enable){
    RAWINPUTDEVICE rid{}; rid.usUsagePage=0x01; rid.usUsage=0x02;
    rid.dwFlags= enable ? (RIDEV_INPUTSINK|RIDEV_CAPTUREMOUSE) : RIDEV_REMOVE; rid.hwndTarget=hwnd;
    RegisterRawInputDevices(&rid,1,sizeof(rid));
}

// --------------------------------------------------------
// Key mapping & WndProc
// --------------------------------------------------------
static KeyCode vk_to_key(WPARAM vk){
    switch(vk){
        case 'W':return Key_W; case 'A':return Key_A; case 'S':return Key_S; case 'D':return Key_D;
        case 'Q':return Key_Q; case 'E':return Key_E;
        case 'Z':return Key_Z; case 'H':return Key_H; case 'G':return Key_G;
        case VK_SPACE:return Key_Space; case VK_ESCAPE:return Key_Escape;
        case VK_UP:return Key_Up; case VK_DOWN:return Key_Down; case VK_LEFT:return Key_Left; case VK_RIGHT:return Key_Right;
        case VK_F1:return Key_F1; case VK_F2:return Key_F2; case VK_F3:return Key_F3; case VK_F4:return Key_F4;
        case VK_F5:return Key_F5; case VK_F6:return Key_F6; case VK_F7:return Key_F7; case VK_F8:return Key_F8; case VK_F9:return Key_F9;
        case VK_F10:return Key_F10; case VK_F11:return Key_F11; case VK_F12:return Key_F12;
        default:return Key_Unknown;
    }
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
    case WM_CREATE: { g_win.dpi=GetDpiForWindow(hwnd); DragAcceptFiles(hwnd, TRUE); } return 0;
    case WM_DPICHANGED:{ g_win.dpi=HIWORD(wParam); RECT* nr=(RECT*)lParam;
        SetWindowPos(hwnd,nullptr,nr->left,nr->top,nr->right-nr->left,nr->bottom-nr->top,SWP_NOZORDER|SWP_NOACTIVATE); } return 0;
    case WM_SIZE:{ if(!g_bb.pixels) g_bb.alloc(g_win.baseW,g_win.baseH); return 0; }
    case WM_MOUSEMOVE: g_in.mouseX=GET_X_LPARAM(lParam); g_in.mouseY=GET_Y_LPARAM(lParam); return 0;
    case WM_MOUSEWHEEL: g_in.wheel += (float)GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA; return 0;
    case WM_LBUTTONDOWN: set_button(g_in.mouseL,true); SetCapture(hwnd); return 0;
    case WM_LBUTTONUP:   set_button(g_in.mouseL,false); ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: set_button(g_in.mouseR,true); SetCapture(hwnd); return 0;
    case WM_RBUTTONUP:   set_button(g_in.mouseR,false); ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN: set_button(g_in.mouseM,true); SetCapture(hwnd); return 0;
    case WM_MBUTTONUP:   set_button(g_in.mouseM,false); ReleaseCapture(); return 0;
    case WM_INPUT:{
        if(!g_in.rawMouse) break;
        UINT size=0; GetRawInputData((HRAWINPUT)lParam,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));
        uint8_t buf[sizeof(RAWINPUT)]{}; RAWINPUT* ri=(RAWINPUT*)buf;
        if(size<=sizeof(buf) && GetRawInputData((HRAWINPUT)lParam,RID_INPUT,ri,&size,sizeof(RAWINPUTHEADER))==size){
            if(ri->header.dwType==RIM_TYPEMOUSE){ g_in.mouseDX += ri->data.mouse.lLastX; g_in.mouseDY += ri->data.mouse.lLastY; }
        }
    } return 0;
    case WM_CHAR:{
        WCHAR wc=(WCHAR)wParam;
        if(wc>=32 && wc<128 && g_in.textLen<(int)sizeof(g_in.text)-1){ g_in.text[g_in.textLen++]=(char)wc; g_in.text[g_in.textLen]=0; }
    } return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:{
        if(wParam==VK_RETURN && (HIWORD(lParam)&KF_ALTDOWN)){ toggle_fullscreen(hwnd); return 0; }
        KeyCode k=vk_to_key(wParam); if(k!=Key_Unknown) set_button(g_in.key[k],true);
    } return 0;
    case WM_SYSKEYUP:
    case WM_KEYUP:{ KeyCode k=vk_to_key(wParam); if(k!=Key_Unknown) set_button(g_in.key[k],false); } return 0;
    case WM_DROPFILES:{ HDROP h=(HDROP)wParam; UINT n=DragQueryFileW(h,0xFFFFFFFF,nullptr,0); (void)n; DragFinish(h); return 0; }
    case WM_CLOSE: g_win.running=false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// --------------------------------------------------------
// Perf HUD + micro lanes + CRC32
// --------------------------------------------------------
static struct PerfHUD{ float frameMS=0, fps=0; float graph[180]{}; int head=0; bool show=true; } g_perf;
struct Micro{ double tUpdate=0, tRender=0, tPost=0, tPresent=0; } g_micro;
static inline uint64_t tic(){ return now_qpc(); }
static inline double toc(uint64_t t0){ return qpc_to_sec(now_qpc()-t0); }

static uint32_t crc32_table[256];
static void crc32_init(){ const uint32_t poly=0xEDB88320; for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int j=0;j<8;j++) c = (c&1)?(poly^(c>>1)):(c>>1); crc32_table[i]=c; } }
static uint32_t crc32_frame(Backbuffer& bb){
    uint32_t c=~0u; for(int y=0;y<bb.h;y++){ const uint8_t* row=(const uint8_t*)rowptr(bb,y);
        for(int x=0;x<bb.w*4;x++){ c = crc32_table[(c ^ row[x]) & 0xFF] ^ (c >> 8); } }
    return ~c;
}
static void draw_perf_hud(Backbuffer& bb){
    if(!g_perf.show) return;
    const int x0=8,y0=8,w=200,h=42;
    fill_rect_scalar(bb,x0-4,y0-16, w+220, h+28, rgba8(0,0,0,160));
    char buf[512];
    _snprintf_s(buf,sizeof(buf),
        "FPS %.1f  %.2f ms [F1 HUD] [F2 int:%s] [F3 vsync:%s] [F4 raw:%s] [F5 pause] [F6 step] [F7 slow] [F8 rec] [F9 play] [F10 dither] [H smooth] [Z magnify]",
        g_perf.fps,g_perf.frameMS, g_win.integerScale?"on":"off", g_win.useVsync?"on":"off", g_in.rawMouse?"on":"off");
    // micro lanes
    char lanes[160];
    _snprintf_s(lanes,sizeof(lanes)," | upd %.2fms ren %.2fms fx %.2fms pr %.2fms",
        g_micro.tUpdate*1000.0, g_micro.tRender*1000.0, g_micro.tPost*1000.0, g_micro.tPresent*1000.0);
    strncat_s(buf, lanes, _TRUNCATE);
    // frame hash
    uint32_t fh=crc32_frame(bb); char hb[32]; _snprintf_s(hb,sizeof(hb),"  hash %08X", fh); strncat_s(buf, hb, _TRUNCATE);
    draw_text6x8(bb,x0,y0,buf,rgb8(255,255,255));
    int gx=x0, gy=y0+10;
    for(int i=0;i<180;i++){
        float ms=g_perf.graph[(g_perf.head+i)%180];
        int bar=clampi((int)ms,0,40);
        for(int y=0;y<bar;y++){
            int yy=gy+(40-1-y); if(yy>=0&&yy<bb.h && gx+i<bb.w) ((uint32_t*)rowptr(bb,yy))[gx+i]=rgb8(180,240,100);
        }
    }
}

// --------------------------------------------------------
// Magnifier overlay
// --------------------------------------------------------
static void draw_magnifier(Backbuffer& bb, int srcX,int srcY,int radiusPx,int scale,bool grid=true){
    int size=radiusPx*2+1, outW=size*scale, outH=size*scale, ox=12, oy=64;
    draw_soft_shadow(bb, ox+4,oy+4, outW+8,outH+8, 6, 64, pack_rgb(0,0,0));
    draw_sdf_roundrect(bb, (float)ox,(float)oy, (float)outW+8,(float)outH+8, 6.f, pack_rgb(22,22,24), 1.0f);
    for(int dy=0; dy<size; ++dy){
        int sy=clampi(srcY + (dy-radiusPx), 0, bb.h-1);
        for(int dx=0; dx<size; ++dx){
            int sx=clampi(srcX + (dx-radiusPx), 0, bb.w-1);
            uint32_t p=((uint32_t*)rowptr(bb,sy))[sx];
            int rx=ox+4+dx*scale, ry=oy+4+dy*scale;
            fill_rect_sse2(bb, rx,ry, scale,scale, p);
            if(grid && scale>=6){
                for(int k=0;k<scale;k++){
                    int gx=rx+k;
                    if(k==0||k==scale-1){
                        if(gx>=0 && gx<bb.w){
                            for(int gy=ry; gy<ry+scale; ++gy) if(gy>=0&&gy<bb.h) ((uint32_t*)rowptr(bb,gy))[gx]=pack_rgb(0,0,0);
                        }
                    }
                }
            }
        }
    }
    int cx=ox+4+radiusPx*scale, cy=oy+4+radiusPx*scale;
    for(int i=0;i<scale;i++){
        int X=cx+i,Y=cy+i;
        if(X>=0&&X<bb.w){ for(int t=0;t<scale;t++){ int yy=cy+t; if(yy>=0&&yy<bb.h) ((uint32_t*)rowptr(bb,yy))[X]=pack_rgb(250,230,90); } }
        if(Y>=0&&Y<bb.h){ for(int t=0;t<scale;t++){ int xx=cx+t; if(xx>=0&&xx<bb.w) ((uint32_t*)rowptr(bb,Y))[xx]=pack_rgb(250,230,90); } }
    }
}

// --------------------------------------------------------
// Demo (decoupled: simulate + render)
// --------------------------------------------------------
struct DemoCtx{ float t=0.f, prev_t=0.f; } g_demo;

static void demo_tile_job(void*,int y0,int y1){
    const int tile=16;
    for(int y=y0;y<y1;y++){
        uint32_t* row=(uint32_t*)rowptr(g_bb,y);
        for(int x=0;x<g_bb.w;x++){
            int tx=x/tile, ty=y/tile; row[x]=tile_color(tx,ty);
        }
    }
}

static void demo_simulate(float dt){
    g_demo.prev_t = g_demo.t;
    g_demo.t += dt;
}

static void demo_render(float alpha){
    const int tileRows=32;
    // MT tile background render
    std::vector<TileJob> jobs; for(int y=0;y<g_bb.h;y+=tileRows) jobs.push_back({ y,clampi(y+tileRows,0,g_bb.h), demo_tile_job,nullptr });
    g_pool.dispatch(jobs); g_pool.wait();

    // UI panel + icon
    draw_soft_shadow(g_bb, 24,24, 220,64, 8, 80, pack_rgb(0,0,0));
    draw_sdf_roundrect(g_bb, 20,20, 220,64, 10.f, pack_rgb(38,40,48), 1.0f);
    draw_sdf_circle(g_bb, 50.f,52.f, 14.5f, pack_rgb(250,230,90), 1.25f);

    // Interpolated moving circle marker
    float t = g_demo.prev_t + (g_demo.t - g_demo.prev_t) * clampf(alpha,0.f,1.f);
    float cx=(sinf(t*0.7f)*0.5f+0.5f)*(g_bb.w-80);
    float cy=(sinf(t*1.1f+1.57f)*0.5f+0.5f)*(g_bb.h-80);
    draw_sdf_circle(g_bb, cx,cy, 14.5f, pack_rgb(232,85,120), 1.25f);

    // Grid
    uint32_t grid=rgb8(0,0,0); const int step=16;
    for(int x=0;x<g_bb.w;x+=step) line(g_bb,x,0,x,g_bb.h-1,grid);
    for(int y=0;y<g_bb.h;y+=step) line(g_bb,0,y,g_bb.w-1,y,grid);

    // Input info
    char info[160];
    _snprintf_s(info,sizeof(info),"Mouse (%d,%d) d(%d,%d) wheel %.1f  Pad0 lx %.2f ly %.2f",
        g_in.mouseX,g_in.mouseY,g_in.mouseDX,g_in.mouseDY,g_in.wheel,g_in.pads[0].lx,g_in.pads[0].ly);
    draw_text6x8(g_bb, 8, g_bb.h-20, info, rgb8(255,255,255));
}

// --------------------------------------------------------
// Screenshot BMP + Clipboard
// --------------------------------------------------------
static bool save_bmp(const char* path){
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    BITMAPFILEHEADER bfh{}; BITMAPINFOHEADER bih{};
    int stride=g_bb.w*4, imageSize=stride*g_bb.h;
    bfh.bfType=0x4D42; bfh.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER); bfh.bfSize=bfh.bfOffBits+imageSize;
    bih.biSize=sizeof(BITMAPINFOHEADER); bih.biWidth=g_bb.w; bih.biHeight=g_bb.h; bih.biPlanes=1; bih.biBitCount=32; bih.biCompression=BI_RGB;
    DWORD wr;
    WriteFile(f,&bfh,sizeof(bfh),&wr,nullptr);
    WriteFile(f,&bih,sizeof(bih),&wr,nullptr);
    for(int y=g_bb.h-1; y>=0; --y){ uint8_t* row=(uint8_t*)rowptr(g_bb,y); WriteFile(f,row,stride,&wr,nullptr); }
    CloseHandle(f); return true;
}
static bool copy_bitmap_to_clipboard(HWND hwnd){
    int sz=g_bb.w*g_bb.h*4 + sizeof(BITMAPINFOHEADER);
    HGLOBAL h=GlobalAlloc(GHND,sz); if(!h) return false;
    uint8_t* mem=(uint8_t*)GlobalLock(h);
    BITMAPINFOHEADER* bih=(BITMAPINFOHEADER*)mem; *bih={};
    bih->biSize=sizeof(BITMAPINFOHEADER); bih->biWidth=g_bb.w; bih->biHeight=-g_bb.h; bih->biPlanes=1; bih->biBitCount=32; bih->biCompression=BI_RGB;
    memcpy(mem+sizeof(BITMAPINFOHEADER), g_bb.pixels, (size_t)g_bb.w*g_bb.h*4);
    GlobalUnlock(h);
    if(OpenClipboard(hwnd)){ EmptyClipboard(); SetClipboardData(CF_DIB,h); CloseClipboard(); return true; } else { GlobalFree(h); return false; }
}

// --------------------------------------------------------
// Platform API impl
// --------------------------------------------------------
static void   plat_log(const char* msg){ OutputDebugStringA(msg); OutputDebugStringA("\n"); }
static double plat_time(){ return qpc_to_sec(now_qpc()); }
static bool   plat_write(const char* path,const void* data,size_t bytes){
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false; DWORD wr=0; BOOL ok=WriteFile(f,data,(DWORD)bytes,&wr,nullptr); CloseHandle(f); return ok && wr==(DWORD)bytes;
}
static bool   plat_read(const char* path,std::vector<uint8_t>* out){
    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false; LARGE_INTEGER sz; GetFileSizeEx(f,&sz); out->resize((size_t)sz.QuadPart);
    DWORD rd=0; BOOL ok=ReadFile(f,out->data(),(DWORD)out->size(),&rd,nullptr); CloseHandle(f); return ok && rd==(DWORD)out->size();
}

// --------------------------------------------------------
// Record / Replay
// --------------------------------------------------------
struct FrameRec{ InputState in; float dt; };
static struct Recorder{ std::vector<FrameRec> frames; bool recording=false, playing=false; size_t idx=0; void clear(){ frames.clear(); recording=playing=false; idx=0; } } g_rec;

// --------------------------------------------------------
// Entry
// --------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int){
    set_dpi_awareness();

    // --- FIX: use WNDCLASSEXW with explicit member initialization to avoid C2078 and match docs.
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInst;
    wc.hIcon         = nullptr; // or LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPLICATION))
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = L"GamePlatformWin32";
    wc.hIconSm       = wc.hIcon;

    RegisterClassExW(&wc); // RegisterClassEx for WNDCLASSEXW (supersedes WNDCLASS)

    DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE; RECT wr{0,0,g_win.baseW,g_win.baseH}; AdjustWindowRect(&wr,style,FALSE);
    HWND hwnd=CreateWindowW(wc.lpszClassName, L"Colony — Ultra Platform", style, CW_USEDEFAULT,CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top, nullptr,nullptr,hInst,nullptr);
    g_win.hwnd=hwnd;

    g_bb.alloc(g_win.baseW,g_win.baseH);
    int hw=(int)std::thread::hardware_concurrency(); g_pool.init( (hw>2)?(hw-1):1 );
    g_in.rawMouse=g_win.enableRawMouse; enable_raw_mouse(hwnd,g_in.rawMouse);

    // ---- High-resolution timer period: request the minimum supported and track it
    TIMECAPS tc{}; if(timeGetDevCaps(&tc,sizeof(tc))==TIMERR_NOERROR){
        UINT desired = clampi(1, (int)tc.wPeriodMin, (int)tc.wPeriodMax);
        if(timeBeginPeriod(desired)==TIMERR_NOERROR) g_timerPeriod = desired;
    }

    LARGE_INTEGER freqLi; QueryPerformanceFrequency(&freqLi); const double invFreq=1.0/(double)freqLi.QuadPart;
    uint64_t tPrev=now_qpc(); double simTime=0.0; double acc=0.0;

    HotReload hot{}; hot.lastWrite=filetimeA("game.dll");
    if(file_existsA("game.dll") && load_game(hot,"game.dll")){
        if(hot.api.bind_platform){
            PlatformAPI plat{}; plat.log_text=plat_log; plat.time_now_sec=plat_time;
            plat.screenshot_bmp = [](const char* p){ return save_bmp(p); };
            plat.clipboard_copy_bitmap = []()->bool{ return copy_bitmap_to_clipboard(g_win.hwnd); };
            plat.file_write_all=plat_write; plat.file_read_all=plat_read;
            hot.api.bind_platform(&plat,1);
        }
        if(hot.api.init)   hot.api.init(&hot.userState, g_bb.w, g_bb.h);
        if(hot.api.resize) hot.api.resize(hot.userState, g_bb.w, g_bb.h);
    }

    HDC hdc=GetDC(hwnd);
    crc32_init();

    bool paused=false, slowmo=false, useDither=false, gamma=false, magnify=false, useDirty=true;

    while(g_win.running){
        // Hot reload
        FILETIME ft=filetimeA("game.dll");
        if( (ft.dwLowDateTime|ft.dwHighDateTime) && CompareFileTime(&ft,&hot.lastWrite)==1 ){
            unload_game(hot); hot.lastWrite=ft;
            if(load_game(hot,"game.dll")){
                if(hot.api.bind_platform){
                    PlatformAPI plat{}; plat.log_text=plat_log; plat.time_now_sec=plat_time;
                    plat.screenshot_bmp = [](const char* p){ return save_bmp(p); };
                    plat.clipboard_copy_bitmap = []()->bool{ return copy_bitmap_to_clipboard(g_win.hwnd); };
                    plat.file_write_all=plat_write; plat.file_read_all=plat_read;
                    hot.api.bind_platform(&plat,1);
                }
                if(hot.api.init) hot.api.init(&hot.userState, g_bb.w, g_bb.h);
                if(hot.api.resize) hot.api.resize(hot.userState, g_bb.w, g_bb.h);
            }
        }

        // Messages / input
        uint64_t tU0=tic();
        MSG msg; begin_frame(g_in);
        while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) g_win.running=false; TranslateMessage(&msg); DispatchMessage(&msg); }
        if(pressed(g_in.key[Key_F1]))  g_perf.show=!g_perf.show;
        if(pressed(g_in.key[Key_F2]))  g_win.integerScale=!g_win.integerScale;
        if(pressed(g_in.key[Key_F3]))  g_win.useVsync=!g_win.useVsync;
        if(pressed(g_in.key[Key_F4])){ g_in.rawMouse=!g_in.rawMouse; enable_raw_mouse(hwnd,g_in.rawMouse); }
        if(pressed(g_in.key[Key_F5]))  paused=!paused;
        if(pressed(g_in.key[Key_F6])){ paused=true; acc += g_win.fixedDT; } // step one frame
        if(pressed(g_in.key[Key_F7]))  slowmo=!slowmo;
        if(pressed(g_in.key[Key_F8])){ g_rec.recording=!g_rec.recording; if(g_rec.recording){ g_rec.frames.clear(); g_rec.playing=false; } }
        if(pressed(g_in.key[Key_F9])){ g_rec.playing=!g_rec.playing; g_rec.idx=0; g_rec.recording=false; }
        if(pressed(g_in.key[Key_F10])) useDither=!useDither;
        if(pressed(g_in.key[Key_F11])) toggle_fullscreen(hwnd);
        if(pressed(g_in.key[Key_F12])){ SYSTEMTIME st; GetLocalTime(&st); char name[128];
            _snprintf_s(name,sizeof(name),"screenshot-%04d%02d%02d-%02d%02d%02d.bmp",
                (int)st.wYear,(int)st.wMonth,(int)st.wDay,(int)st.wHour,(int)st.wMinute,(int)st.wSecond);
            save_bmp(name); copy_bitmap_to_clipboard(hwnd); }
        if(pressed(g_in.key[Key_H])) g_win.smoothScale=!g_win.smoothScale;
        if(pressed(g_in.key[Key_G])) gamma=!gamma;
        if(pressed(g_in.key[Key_Z])) magnify=!magnify;

        poll_gamepads(g_in);
        g_micro.tUpdate=toc(tU0);

        // Timing
        uint64_t tNow=now_qpc(); double dt=(double)(tNow-tPrev)*invFreq; tPrev=tNow; if(slowmo) dt*=0.25;

        // Replay/Record feed (overrides dt/inputs if playing)
        if(g_rec.playing){
            if(g_rec.idx<g_rec.frames.size()){
                g_in = g_rec.frames[g_rec.idx].in; dt = g_rec.frames[g_rec.idx].dt; g_rec.idx++;
            } else { g_rec.playing=false; }
        }

        if(g_win.fixedTimestep){ acc += dt; } else { acc = dt; }

        // -----------------------------
        // Simulate (0..N times), Render once with alpha
        // -----------------------------
        bool rendered_by_fallback = false;
        g_dirty.clear();

        const double step = g_win.fixedDT;
        float alpha = 1.0f;

        if(g_win.fixedTimestep){
            if(!paused){
                int safety = 0;
                // Clamp catch-up (implicit via iteration cap or add an explicit clamp if desired)
                while(acc >= step && safety < 16){
                    // --- Simulation-only path preferred
                    if(hot.active && hot.api.update_fixed){
                        hot.api.update_fixed(hot.userState, (float)step);
                    } else if(hot.active && hot.api.update_and_render){
                        // Fallback: legacy combined step renders each update
                        hot.api.update_and_render(hot.userState, (float)step, (uint32_t*)g_bb.pixels, g_bb.w, g_bb.h, &g_in);
                        rendered_by_fallback = true;
                    } else {
                        // Demo simulate
                        demo_simulate((float)step);
                    }
                    simTime += step; acc -= step; safety++;
                    if(g_rec.recording){ g_rec.frames.push_back(FrameRec{ g_in, (float)step }); }
                }
            }
            alpha = (float)clampf((float)(acc/step), 0.f, 1.f);
        }else{
            // Variable step
            if(!paused){
                if(hot.active && hot.api.update_fixed){
                    hot.api.update_fixed(hot.userState, (float)acc);
                } else if(hot.active && hot.api.update_and_render){
                    hot.api.update_and_render(hot.userState, (float)acc, (uint32_t*)g_bb.pixels, g_bb.w, g_bb.h, &g_in);
                    rendered_by_fallback = true;
                } else {
                    demo_simulate((float)acc);
                }
                simTime += acc; if(g_rec.recording){ g_rec.frames.push_back(FrameRec{ g_in, (float)acc }); }
            }
            alpha = 1.0f;
            acc = 0.0;
        }

        // ---- Render once (decoupled), unless the legacy fallback already rendered
        uint64_t tR0=tic();
        if(!rendered_by_fallback){
            if(hot.active && hot.api.render){
                hot.api.render(hot.userState, alpha, (uint32_t*)g_bb.pixels, g_bb.w, g_bb.h, &g_in);
                useDirty=false;
            } else if(!hot.active){
                // Demo single render with interpolation
                demo_render(alpha);
                useDirty=false;
            } else {
                // Legacy combined API but no steps ran (acc < step): draw a zero-dt frame
                if(hot.api.update_and_render){
                    hot.api.update_and_render(hot.userState, 0.0f, (uint32_t*)g_bb.pixels, g_bb.w, g_bb.h, &g_in);
                    useDirty=false;
                }
            }
        }
        g_micro.tRender=toc(tR0);

        // ---- Post (dither, magnifier, HUD)
        uint64_t tP0=tic();
        if(useDither) apply_dither_gamma(g_bb, gamma);
        if(magnify)   draw_magnifier(g_bb, g_in.mouseX,g_in.mouseY, 10,8,true);
        // Show HUD using wall-clock delta for smoother UX
        g_perf.frameMS = (float)(dt*1000.0);
        draw_perf_hud(g_bb);
        g_micro.tPost=toc(tP0);

        // ---- Present
        uint64_t tPr0=tic();
        if(useDirty) present_dirty(hwnd,hdc,g_bb,g_dirty); else present_full(hwnd,hdc,g_bb);
        if(g_win.useVsync){ BOOL comp=FALSE; DwmIsCompositionEnabled(&comp); if(comp) DwmFlush(); }
        g_micro.tPresent=toc(tPr0);

        // HUD fps graph (based on wall dt)
        double walldt=dt; g_perf.fps=(float)(1.0/(walldt>1e-6?walldt:1.0/1000.0));
        g_perf.graph[g_perf.head=(g_perf.head+1)%180] = (float)(walldt*1000.0);

        // Pace a bit in variable-step
        if(!g_win.fixedTimestep){
            double tAfter=qpc_to_sec(now_qpc()); double target=1.0/60.0;
            double remain = target - (tAfter - (simTime - acc));
            if(remain>0.001){ Sleep((DWORD)(remain*1000.0)); }
        }
    }

    ReleaseDC(hwnd,hdc);
    g_pool.shutdown();
    g_bb.free();
    unload_game(hot);
    if(g_timerPeriod) timeEndPeriod(g_timerPeriod);
    return 0;
}
