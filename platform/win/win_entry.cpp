// platform/win/win_entry.cpp
// Windows-only, zero-dependency entry + loop. Pairs with platform/win/win_present_gdi.cpp

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <xinput.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>   // (E) prefer <cstring> and ::memcpy/::memset

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "xinput9_1_0.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

// ---------- Forward declarations from win_present_gdi.cpp ---------------------
struct PresentConfig{ bool integerScale; bool smoothScale; };
void gdi_present_full(HWND hwnd, HDC hdc, int backW, int backH,
                      const void* pixels, const BITMAPINFO* bmi,
                      const PresentConfig& cfg);
void gdi_present_dirty(HWND hwnd, HDC hdc, int backW, int backH,
                       const void* pixels, const BITMAPINFO* bmi,
                       const RECT* rects, size_t numRects,
                       const PresentConfig& cfg);
// -----------------------------------------------------------------------------

// 🔸 Include shared input API (Option B)
#include "win_input.h"

// --------------------------------- Utils -------------------------------------
static inline int  clampi(int v, int lo, int hi){ return (v<lo)?lo:((v>hi)?hi:v); }
static inline float clampf(float v, float lo, float hi){ return (v<lo)?lo:((v>hi)?hi:v); }
static inline uint32_t rgb8(uint8_t r,uint8_t g,uint8_t b){ return (uint32_t)b<<16 | (uint32_t)g<<8 | (uint32_t)r; }
static inline uint32_t rgba8(uint8_t r,uint8_t g,uint8_t b,uint8_t a){ return (uint32_t)a<<24 | rgb8(r,g,b); }

static inline uint64_t now_qpc(){ LARGE_INTEGER li; QueryPerformanceCounter(&li); return (uint64_t)li.QuadPart; }
static inline double   qpc_to_sec(uint64_t t){ static double inv=0; if(!inv){ LARGE_INTEGER f; QueryPerformanceFrequency(&f); inv=1.0/(double)f.QuadPart; } return t*inv; }
static inline uint32_t hash32(uint32_t x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b){ return (uint32_t)b<<16 | (uint32_t)g<<8 | (uint32_t)r; }

// ------------------------------- Backbuffer ----------------------------------
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

// ------------------------------ Tiny bitmap font -----------------------------
#define GLYPH6x8(...) {__VA_ARGS__}
static const uint8_t kFont6x8[96][8] = {
    GLYPH6x8(0,0,0,0,0,0,0,0), GLYPH6x8(0x30,0x30,0x30,0x30,0x30,0,0x30,0),
    GLYPH6x8(0x6c,0x6c,0x48,0,0,0,0,0),   GLYPH6x8(0x6c,0xfe,0x6c,0x6c,0xfe,0x6c,0,0),
    GLYPH6x8(0x10,0x7c,0x90,0x78,0x14,0xf8,0x10,0), GLYPH6x8(0xc4,0xc8,0x10,0x20,0x46,0x86,0,0),
    GLYPH6x8(0x30,0x48,0x30,0x52,0x8c,0xcc,0x76,0), GLYPH6x8(0x30,0x30,0x20,0,0,0,0,0),
    GLYPH6x8(0x18,0x30,0x60,0x60,0x60,0x30,0x18,0), GLYPH6x8(0x60,0x30,0x18,0x18,0x18,0x30,0x60,0),
    GLYPH6x8(0,0x44,0x38,0xfe,0x38,0x44,0,0),       GLYPH6x8(0,0x10,0x10,0x7c,0x10,0x10,0,0),
    GLYPH6x8(0,0,0,0,0,0x30,0x30,0x20),             GLYPH6x8(0,0,0,0x7c,0,0,0,0),
    GLYPH6x8(0,0,0,0,0,0x30,0x30,0),                GLYPH6x8(0x04,0x08,0x10,0x20,0x40,0x80,0,0),
    GLYPH6x8(0x7c,0x82,0x8a,0x92,0xa2,0x82,0x7c,0), GLYPH6x8(0x10,0x30,0x10,0x10,0x10,0x10,0x7c,0),
    GLYPH6x8(0x7c,0x82,0x04,0x18,0x60,0x80,0xfe,0), GLYPH6x8(0x7c,0x82,0x04,0x38,0x04,0x82,0x7c,0),
    GLYPH6x8(0x08,0x18,0x28,0x48,0x88,0xfe,0x08,0), GLYPH6x8(0xfe,0x80,0x80,0xfc,0x02,0x02,0x82,0x7c,0),
    GLYPH6x8(0x3c,0x40,0x80,0xfc,0x82,0x82,0x7c,0), GLYPH6x8(0xfe,0x82,0x04,0x08,0x10,0x10,0x10,0),
    GLYPH6x8(0x7c,0x82,0x82,0x7c,0x82,0x82,0x7c,0), GLYPH6x8(0x7c,0x82,0x82,0x7e,0x02,0x04,0x78,0),
    GLYPH6x8(0,0x30,0x30,0,0x30,0x30,0,0),          GLYPH6x8(0,0x30,0x30,0,0x30,0x30,0x20,0),
    GLYPH6x8(0x0c,0x30,0xc0,0x30,0x0c,0,0,0),       GLYPH6x8(0,0x7c,0,0x7c,0,0,0,0),
    GLYPH6x8(0xc0,0x30,0x0c,0x30,0xc0,0,0,0),       GLYPH6x8(0x7c,0x82,0x04,0x18,0x10,0,0x10,0),
    GLYPH6x8(0x7c,0x82,0xba,0xaa,0xbe,0x80,0x7c,0), GLYPH6x8(0x38,0x44,0x82,0xfe,0x82,0x82,0x82,0),
    GLYPH6x8(0xfc,0x82,0x82,0xfc,0x82,0x82,0xfc,0), GLYPH6x8(0x7c,0x82,0x80,0x80,0x80,0x82,0x7c,0),
    GLYPH6x8(0xf8,0x84,0x82,0x82,0x82,0x84,0xf8,0), GLYPH6x8(0xfe,0x80,0x80,0xfc,0x80,0x80,0xfe,0),
    GLYPH6x8(0xfe,0x80,0x80,0xfc,0x80,0x80,0x80,0), GLYPH6x8(0x7c,0x82,0x80,0x8e,0x82,0x82,0x7e,0),
    GLYPH6x8(0x82,0x82,0x82,0xfe,0x82,0x82,0x82,0), GLYPH6x8(0x7c,0x10,0x10,0x10,0x10,0x10,0x7c,0),
    GLYPH6x8(0x3e,0x04,0x04,0x04,0x84,0x84,0x78,0), GLYPH6x8(0x82,0x84,0x88,0xf0,0x88,0x84,0x82,0),
    GLYPH6x8(0x80,0x80,0x80,0x80,0x80,0x80,0xfe,0), GLYPH6x8(0x82,0xc6,0xaa,0x92,0x82,0x82,0x82,0),
    GLYPH6x8(0x82,0xc2,0xa2,0x92,0x8a,0x86,0x82,0), GLYPH6x8(0x7c,0x82,0x82,0x82,0x82,0x82,0x7c,0),
    GLYPH6x8(0xfc,0x82,0x82,0xfc,0x80,0x80,0x80,0), GLYPH6x8(0x7c,0x82,0x82,0x82,0x92,0x8c,0x7e,0),
    GLYPH6x8(0x7c,0x80,0x7c,0x02,0x02,0x82,0x7c,0),
    GLYPH6x8(0xfe,0x10,0x10,0x10,0x10,0x10,0x10,0), GLYPH6x8(0x82,0x82,0x82,0x82,0x82,0x82,0x7c,0),
    GLYPH6x8(0x82,0x82,0x44,0x44,0x28,0x28,0x10,0), GLYPH6x8(0x82,0x92,0xaa,0xc6,0x82,0x82,0x82,0),
    GLYPH6x8(0x82,0x44,0x28,0x10,0x10,0x10,0x10,0),
    GLYPH6x8(0xfe,0x04,0x08,0x30,0x40,0x80,0xfe,0)
};
#undef GLYPH6x8

static void draw_text6x8(Backbuffer& bb,int x,int y,const char* s,uint32_t c){
    for(;*s;++s,x+=6){
        char ch=*s; if(ch<32||ch>127) ch='?';
        const uint8_t* g=kFont6x8[ch-32];
        if(y<0||y+8>bb.h) return;
        for(int row=0;row<8;++row){
            uint8_t bits=g[row]; if(bits==0) continue;
            int ry=y+row; if(ry<0||ry>=bb.h) continue;
            uint32_t* dst=(uint32_t*)rowptr(bb,ry);
            for(int col=0;col<6;++col) if((bits>>col)&1){
                int xx=x+col; if(xx>=0&&xx<bb.w) dst[xx]=c;
            }
        }
    }
}

// ------------------------- Procedural + tiny demo ----------------------------
static inline uint32_t tile_color(int tx,int ty){
    uint32_t h = hash32((uint32_t)tx*73856093u ^ (uint32_t)ty*19349663u);
    uint8_t r=(uint8_t)(128+(h&63)), g=(uint8_t)(80+((h>>8)&127)), b=(uint8_t)(80+((h>>16)&127));
    return rgb8(r,g,b);
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

// ----------------------------- Thread pool -----------------------------------
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

// ------------------------------ Dirty rectangles -----------------------------
struct Dirty{ RECT r; };
struct DirtyTracker{
    std::vector<Dirty> rects;
    void clear(){ rects.clear(); }
    void mark(int x,int y,int w,int h){
        if(w<=0||h<=0) return;
        RECT r{ x,y,x+w,y+h };
        rects.push_back(Dirty{r});
        if(rects.size()>256){ rects.clear(); rects.push_back(Dirty{ RECT{0,0,INT32_MAX,INT32_MAX} }); }
    }
};

// ------------------------- Platform/Game API + hot reload --------------------
struct PlatformAPI{
    void (*log_text)(const char* msg)=nullptr;
    double (*time_now_sec)()=nullptr;
    bool (*screenshot_bmp)(const char* path)=nullptr;
    bool (*clipboard_copy_bitmap)()=nullptr;
    bool (*file_write_all)(const char* path, const void* data, size_t bytes)=nullptr;
    bool (*file_read_all)(const char* path, std::vector<uint8_t>* out)=nullptr;
};

struct GameAPI{
    void (*init)(void** user, int w, int h)=nullptr;
    void (*resize)(void* user, int w, int h)=nullptr;
    void (*update_and_render)(void* user, float dt, uint32_t* pixels, int w, int h, const InputState* input)=nullptr;
    void (*bind_platform)(PlatformAPI* plat, int version)=nullptr;
    void (*update_fixed)(void* user, float dt)=nullptr;
    void (*render)(void* user, float alpha, uint32_t* pixels, int w, int h, const InputState* input)=nullptr;
};

struct HotReload{
    HMODULE dll=nullptr; FILETIME lastWrite{}; GameAPI api{}; void* userState=nullptr; bool active=false;
};
static FILETIME filetimeA(const char* path){ WIN32_FILE_ATTRIBUTE_DATA d{}; if(GetFileAttributesExA(path,GetFileExInfoStandard,&d)) return d.ftLastWriteTime; FILETIME z{}; return z; }
static bool file_existsA(const char* path){ DWORD a=GetFileAttributesA(path); return (a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY)); }
static bool load_game(HotReload& hr, const char* dllName){
    char tmp[MAX_PATH]; wsprintfA(tmp,"%s_hot.dll",dllName); CopyFileA(dllName,tmp,FALSE);
    HMODULE dll=LoadLibraryA(tmp); if(!dll) return false;
    auto init =(void(*)(void**,int,int))GetProcAddress(dll,"game_init");
    auto resize=(void(*)(void*,int,int))GetProcAddress(dll,"game_resize");
    auto step =(void(*)(void*,float,uint32_t*,int,int,const InputState*))GetProcAddress(dll,"game_update_and_render");
    auto bind =(void(*)(PlatformAPI*,int))GetProcAddress(dll,"game_bind_platform");
    auto upf  =(void(*)(void*,float))GetProcAddress(dll,"game_update_fixed");
    auto rend =(void(*)(void*,float,uint32_t*,int,int,const InputState*))GetProcAddress(dll,"game_render");
    if(!step && !upf && !rend){ FreeLibrary(dll); DeleteFileA(tmp); return false; }
    hr.dll=dll; hr.api={}; hr.api.init=init; hr.api.resize=resize; hr.api.update_and_render=step;
    hr.api.bind_platform=bind; hr.api.update_fixed=upf; hr.api.render=rend; hr.active=true; return true;
}
static void unload_game(HotReload& hr){ if(hr.dll){ FreeLibrary(hr.dll); hr.dll=nullptr; } hr.api={}; hr.userState=nullptr; hr.active=false; }

// ------------------------------ Window state/DPI -----------------------------
struct WindowState{
    HWND hwnd=nullptr; bool running=true;
    bool useVsync=true; bool integerScale=true; bool borderless=false; bool enableRawMouse=true;
    bool fixedTimestep=false; float fixedDT=1.f/60.f;
    bool smoothScale=false;
    int baseW=1280, baseH=720; UINT dpi=96;
} g_win;

// DPI awareness: PMv2 -> PMv1 (SHCore) -> system DPI
static void set_dpi_awareness(){
    HMODULE user = GetModuleHandleA("user32.dll");
    if(user){
        typedef BOOL (WINAPI *SetDpiCtx)(HANDLE);
        auto p = (SetDpiCtx)GetProcAddress(user,"SetProcessDpiAwarenessContext");
        if(p){ if(p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return; } // Win10+ PMv2
    }
    HMODULE shcore = LoadLibraryA("SHCore.dll");
    if(shcore){
        typedef HRESULT (WINAPI *SetPDA)(int);
        auto sp = (SetPDA)GetProcAddress(shcore,"SetProcessDpiAwareness");
        if(sp){
            const int PROCESS_PER_MONITOR_DPI_AWARE = 2;
            if(SUCCEEDED(sp(PROCESS_PER_MONITOR_DPI_AWARE))){ FreeLibrary(shcore); return; }
        }
        FreeLibrary(shcore);
    }
    SetProcessDPIAware();
}

static void toggle_fullscreen(HWND hwnd){
    static WINDOWPLACEMENT prev{};
    prev.length = sizeof(prev); // required by Get/SetWindowPlacement. 

    DWORD style=(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE);
    if(!g_win.borderless){
        MONITORINFO mi{}; mi.cbSize = sizeof(mi); // required by GetMonitorInfo. 
        GetWindowPlacement(hwnd,&prev);
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

// -------------------------------- XInput + raw mouse -------------------------
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
static void enable_raw_mouse(HWND hwnd,bool enable){
    RAWINPUTDEVICE rid{}; rid.usUsagePage=0x01; rid.usUsage=0x02;
    rid.dwFlags= enable ? (RIDEV_INPUTSINK|RIDEV_CAPTUREMOUSE) : RIDEV_REMOVE; // register before WM_INPUT. 
    rid.hwndTarget=hwnd;
    RegisterRawInputDevices(&rid,1,sizeof(rid));
}

// ---------------------------------- WndProc ----------------------------------
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
        // Use the suggested RECT per Microsoft HIDPI guidance. 
        SetWindowPos(hwnd,nullptr,nr->left,nr->top,nr->right-nr->left,nr->bottom-nr->top,SWP_NOZORDER|SWP_NOACTIVATE); } return 0;
    case WM_SIZE:{ return 0; }
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

// ------------------------------- Demo content + globals (C) ------------------
static Backbuffer   g_bb;
static ThreadPool   g_pool;
static DirtyTracker g_dirty;
static UINT         g_timerPeriod = 0;

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
static void demo_simulate(float dt){ g_demo.prev_t=g_demo.t; g_demo.t+=dt; }
static void demo_render(float /*alpha*/){
    const int tileRows=32;
    std::vector<TileJob> jobs; for(int y=0;y<g_bb.h;y+=tileRows) jobs.push_back({ y,clampi(y+tileRows,0,g_bb.h), demo_tile_job,nullptr });
    g_pool.dispatch(jobs); g_pool.wait();

    const int step=16;
    for(int x=0;x<g_bb.w;x+=step) line(g_bb,x,0,x,g_bb.h-1, rgb8(0,0,0));
    for(int y=0;y<g_bb.h;y+=step) line(g_bb,0,y,g_bb.w-1,y, rgb8(0,0,0));

    char info[160];
    _snprintf_s(info,sizeof(info),"Mouse (%d,%d) d(%d,%d) wheel %.1f",
        g_in.mouseX,g_in.mouseY,g_in.mouseDX,g_in.mouseDY,g_in.wheel);
    draw_text6x8(g_bb, 8, g_bb.h-20, info, rgb8(255,255,255));
}

// --------------------------------- HUD/CRC -----------------------------------
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
static void draw_perf_hud(Backbuffer& bb, double dtMs){
    if(!g_perf.show) return;
    const int x0=8,y0=8;
    char buf[512];
    _snprintf_s(buf,sizeof(buf),"FPS %.1f  %.2f ms [F1 HUD] [F2 int:%s] [F3 vsync:%s] [F4 raw:%s] [H smooth]",
        g_perf.fps,(float)dtMs, g_win.integerScale?"on":"off", g_win.useVsync?"on":"off", g_in.rawMouse?"on":"off");
    draw_text6x8(bb,x0,y0,buf,rgb8(255,255,255));
    uint32_t fh=crc32_frame(bb); char hb[32]; _snprintf_s(hb,sizeof(hb),"  hash %08X", fh); draw_text6x8(bb,x0,y0+10,hb,rgb8(200,240,120));
    g_perf.graph[g_perf.head=(g_perf.head+1)%180]=(float)dtMs;
}

// ---------- File-scope helpers used in PlatformAPI (no unary '+' needed) -----
static bool SaveBackbufferBMP(const char* path){
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    BITMAPFILEHEADER bfh{}; BITMAPINFOHEADER bih{};
    int stride=g_bb.w*4, imageSize=stride*g_bb.h;
    bfh.bfType=0x4D42; // 'BM'
    bfh.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
    bfh.bfSize=bfh.bfOffBits+imageSize;
    bih.biSize=sizeof(BITMAPINFOHEADER); bih.biWidth=g_bb.w; bih.biHeight=g_bb.h;
    bih.biPlanes=1; bih.biBitCount=32; bih.biCompression=BI_RGB;
    DWORD wr;
    WriteFile(f,&bfh,sizeof(bfh),&wr,nullptr);
    WriteFile(f,&bih,sizeof(bih),&wr,nullptr);
    for(int y=g_bb.h-1;y>=0;--y){ uint8_t* row=(uint8_t*)rowptr(g_bb,y); WriteFile(f,row,stride,&wr,nullptr); }
    CloseHandle(f); return true;
}
static bool CopyBackbufferToClipboard(){
    int sz=g_bb.w*g_bb.h*4 + sizeof(BITMAPINFOHEADER);
    HGLOBAL h=GlobalAlloc(GHND,sz); if(!h) return false; uint8_t* mem=(uint8_t*)GlobalLock(h);
    BITMAPINFOHEADER* bih=(BITMAPINFOHEADER*)mem; ::memset(bih,0,sizeof(BITMAPINFOHEADER));
    bih->biSize=sizeof(BITMAPINFOHEADER); bih->biWidth=g_bb.w; bih->biHeight=-g_bb.h;
    bih->biPlanes=1; bih->biBitCount=32; bih->biCompression=BI_RGB;
    ::memcpy(mem+sizeof(BITMAPINFOHEADER), g_bb.pixels, (size_t)g_bb.w*g_bb.h*4);
    GlobalUnlock(h);
    if(OpenClipboard(g_win.hwnd)){ EmptyClipboard(); SetClipboardData(CF_DIB,h); CloseClipboard(); return true; }
    GlobalFree(h); return false;
}

// ----------------------------------- Entry -----------------------------------
int APIENTRY wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int){
    // High-DPI awareness before any window is created.
    set_dpi_awareness(); // PMv2 -> PMv1 -> system.

    // (A) Register window class using WNDCLASSEXW (explicit assignments).
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc);                 // must set cbSize. 
    wc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"GamePlatformWin32"; wc.hIconSm=wc.hIcon;
    RegisterClassExW(&wc);

    DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;
    RECT wr{0,0,g_win.baseW,g_win.baseH}; AdjustWindowRect(&wr,style,FALSE);
    HWND hwnd=CreateWindowW(wc.lpszClassName, L"Colony — Ultra Platform", style,
                            CW_USEDEFAULT,CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top,
                            nullptr,nullptr,hInst,nullptr);
    g_win.hwnd=hwnd;

    // Backbuffer + threads
    g_bb.alloc(g_win.baseW,g_win.baseH);
    int hw=(int)std::thread::hardware_concurrency(); g_pool.init((hw>2)?(hw-1):1);

    // Raw mouse default
    g_in.rawMouse=g_win.enableRawMouse; enable_raw_mouse(hwnd,g_in.rawMouse);

    // Hi-res timer period (track exact value for proper end)
    TIMECAPS tc{}; if(timeGetDevCaps(&tc,sizeof(tc))==TIMERR_NOERROR){
        UINT desired = clampi(1,(int)tc.wPeriodMin,(int)tc.wPeriodMax);
        if(timeBeginPeriod(desired)==TIMERR_NOERROR) g_timerPeriod = desired;
    }

    LARGE_INTEGER freqLi; QueryPerformanceFrequency(&freqLi); const double invFreq=1.0/(double)freqLi.QuadPart;
    uint64_t tPrev=now_qpc(); double simTime=0.0; double acc=0.0;

    // Hot-reload
    HotReload hot{}; hot.lastWrite=filetimeA("game.dll");
    if(file_existsA("game.dll") && load_game(hot,"game.dll")){
        auto plat_log=[](const char* s){ OutputDebugStringA(s); OutputDebugStringA("\n"); };
        auto plat_time=[]()->double{ return qpc_to_sec(now_qpc()); };
        auto plat_write=[](const char* path,const void* data,size_t bytes)->bool{
            HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f==INVALID_HANDLE_VALUE) return false; DWORD wr=0; BOOL ok=WriteFile(f,data,(DWORD)bytes,&wr,nullptr); CloseHandle(f); return ok && wr==(DWORD)bytes;
        };
        auto plat_read=[](const char* path,std::vector<uint8_t>* out)->bool{
            HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f==INVALID_HANDLE_VALUE) return false; LARGE_INTEGER sz; GetFileSizeEx(f,&sz); out->resize((size_t)sz.QuadPart);
            DWORD rd=0; BOOL ok=ReadFile(f,out->data(),(DWORD)out->size(),&rd,nullptr); CloseHandle(f); return ok && rd==(DWORD)out->size();
        };

        if(hot.api.bind_platform){
            PlatformAPI plat{}; plat.log_text=plat_log; plat.time_now_sec=plat_time;
            // (B) C2088-safe: plain functions, no unary '+'
            plat.screenshot_bmp = &SaveBackbufferBMP;
            plat.clipboard_copy_bitmap = &CopyBackbufferToClipboard;
            plat.file_write_all=plat_write; plat.file_read_all=plat_read;
            hot.api.bind_platform(&plat,1);
        }
        if(hot.api.init)   hot.api.init(&hot.userState, g_bb.w, g_bb.h);
        if(hot.api.resize) hot.api.resize(hot.userState, g_bb.w, g_bb.h);
    }

    HDC hdc=GetDC(hwnd);
    crc32_init();

    bool paused=false, slowmo=false, useDirty=true;

    while(g_win.running){
        // Hot reload check
        FILETIME ft=filetimeA("game.dll");
        if( (ft.dwLowDateTime|ft.dwHighDateTime) && CompareFileTime(&ft,&hot.lastWrite)==1 ){
            unload_game(hot); hot.lastWrite=ft;
            if(load_game(hot,"game.dll")){
                auto plat_log=[](const char* s){ OutputDebugStringA(s); OutputDebugStringA("\n"); };
                auto plat_time=[]()->double{ return qpc_to_sec(now_qpc()); };
                auto plat_write=[](const char* path,const void* data,size_t bytes)->bool{
                    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(f==INVALID_HANDLE_VALUE) return false; DWORD wr=0; BOOL ok=WriteFile(f,data,(DWORD)bytes,&wr,nullptr); CloseHandle(f); return ok && wr==(DWORD)bytes;
                };
                auto plat_read=[](const char* path,std::vector<uint8_t>* out)->bool{
                    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(f==INVALID_HANDLE_VALUE) return false; LARGE_INTEGER sz; GetFileSizeEx(f,&sz); out->resize((size_t)sz.QuadPart);
                    DWORD rd=0; BOOL ok=ReadFile(f,out->data(),(DWORD)out->size(),&rd,nullptr); CloseHandle(f); return ok && rd==(DWORD)out->size();
                };
                if(hot.api.bind_platform){
                    PlatformAPI plat{}; plat.log_text=plat_log; plat.time_now_sec=plat_time;
                    plat.screenshot_bmp = &SaveBackbufferBMP;            // (B)
                    plat.clipboard_copy_bitmap = &CopyBackbufferToClipboard;
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
        if(pressed(g_in.key[Key_H]))  g_win.smoothScale=!g_win.smoothScale;

        poll_gamepads(g_in);
        g_micro.tUpdate=toc(tU0);

        // Timing
        uint64_t tNow=now_qpc(); double dt=(double)(tNow-tPrev)*invFreq; tPrev=tNow; if(slowmo) dt*=0.25;
        if(g_win.fixedTimestep){ acc += dt; } else { acc = dt; }

        // Simulate 0..N times, render once
        g_dirty.clear();
        bool rendered_by_fallback=false;
        const double step = g_win.fixedDT;
        float alpha=1.0f;

        if(g_win.fixedTimestep){
            if(!paused){
                int safety=0;
                while(acc>=step && safety<16){
                    if(hot.active && hot.api.update_fixed){
                        hot.api.update_fixed(hot.userState,(float)step);
                    }else if(hot.active && hot.api.update_and_render){
                        hot.api.update_and_render(hot.userState,(float)step,(uint32_t*)g_bb.pixels,g_bb.w,g_bb.h,&g_in);
                        rendered_by_fallback=true;
                    }else{
                        demo_simulate((float)step);
                    }
                    simTime+=step; acc-=step; safety++;
                }
            }
            alpha=(float)clampf((float)(acc/step),0.f,1.f);
        }else{
            if(!paused){
                if(hot.active && hot.api.update_fixed){
                    hot.api.update_fixed(hot.userState,(float)acc);
                }else if(hot.active && hot.api.update_and_render){
                    hot.api.update_and_render(hot.userState,(float)acc,(uint32_t*)g_bb.pixels,g_bb.w,g_bb.h,&g_in);
                    rendered_by_fallback=true;
                }else{
                    demo_simulate((float)acc);
                }
                simTime+=acc;
            }
            alpha=1.0f; acc=0.0;
        }

        // Render once (decoupled) unless fallback rendered.
        uint64_t tR0=tic();
        if(!rendered_by_fallback){
            if(hot.active && hot.api.render){
                hot.api.render(hot.userState,alpha,(uint32_t*)g_bb.pixels,g_bb.w,g_bb.h,&g_in);
                useDirty=false;
            }else if(!hot.active){
                demo_render(alpha);
                useDirty=false;
            }else{
                if(hot.api.update_and_render){
                    hot.api.update_and_render(hot.userState,0.0f,(uint32_t*)g_bb.pixels,g_bb.w,g_bb.h,&g_in);
                    useDirty=false;
                }
            }
        }
        g_micro.tRender=toc(tR0);

        // HUD
        g_perf.frameMS=(float)(dt*1000.0);
        draw_perf_hud(g_bb, g_perf.frameMS);

        // Present  (D)
        uint64_t tP0=tic();
        PresentConfig cfg{ g_win.integerScale, g_win.smoothScale };
        if(useDirty){
            std::vector<RECT> rects; rects.reserve(g_dirty.rects.size());
            for(const auto& d : g_dirty.rects) rects.push_back(d.r);
            gdi_present_dirty(hwnd, hdc, g_bb.w, g_bb.h, g_bb.pixels, &g_bb.bmi,
                              rects.empty()?nullptr:rects.data(), rects.size(), cfg);
        }else{
            gdi_present_full(hwnd, hdc, g_bb.w, g_bb.h, g_bb.pixels, &g_bb.bmi, cfg);
        }
        if(g_win.useVsync){ BOOL comp=FALSE; DwmIsCompositionEnabled(&comp); if(comp) DwmFlush(); }
        g_micro.tPresent=toc(tP0);

        // FPS
        g_perf.fps=(float)(1.0/(dt>1e-6?dt:1.0/1000.0));

        // Soft pace for variable-step users
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
