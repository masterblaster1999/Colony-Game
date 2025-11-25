// ============================================================================
// src-gamesingletu.cpp
// Single translation unit gameplay module for Colony-Game (Windows-only).
//
// Public surface (callable from Launcher.cpp without a header):
//   struct GameOptions { int width, height; bool fullscreen, vsync, safeMode;
//                        uint64_t seed; std::string profile, lang, saveDir, assetsDir; };
//   int RunColonyGame(const GameOptions&);
//
// This TU owns: window loop, input, world gen, A* pathfinding, colonists & jobs,
// buildings/economy, HUD, and save/load — all using Win32 + GDI (no external deps).
//
// Integration steps:
//   1) Add this file to the build.
//   2) In Launcher.cpp (after you computed effective settings/paths), build a GameOptions
//      instance and call RunColonyGame(go).
//   3) (Optional) Keep --validate in your launcher to check assets folder; this TU
//      does not implement validation because it is called post-bootstrap.
//
// Platform:
//   Windows 10+ (uses DPI awareness and Common Controls). Pure Win32; no CONSOLE.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cassert>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <random>
#include <deque>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Enable v6 Common Controls visual styles without a .manifest
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// =============================== Public Interface ============================

struct GameOptions {
    int         width        = 1280;
    int         height       = 720;
    bool        fullscreen   = false;
    bool        vsync        = true;
    bool        safeMode     = false;
    uint64_t    seed         = 0;
    std::string profile      = "default";
    std::string lang         = "en-US";
    std::string saveDir;     // e.g. %LOCALAPPDATA%\MarsColonySim\Saves
    std::string assetsDir;   // e.g. .\assets
};

int RunColonyGame(const GameOptions& opts); // implemented at bottom

// ================================ Utilities ==================================

namespace util {

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring NowStampCompact() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

template <typename T> static T clamp(T v, T lo, T hi) {
    return std::min(hi, std::max(lo, v));
}

struct Timer {
    LARGE_INTEGER freq{}, last{};
    double acc = 0.0;
    Timer() { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&last); }
    double Tick() {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now; acc += dt; return dt;
    }
    void ClearAcc() { acc = 0.0; }
};

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}

static bool EnsureDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS;
}

} // namespace util

// ================================ Logging ====================================

class Logger {
public:
    bool Open(const std::wstring& logfile) {
        f_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        return f_.is_open();
    }
    void Line(const std::wstring& s) {
        if (!f_) return;
        auto t = util::NowStampCompact();
        std::wstring w = L"[" + t + L"] " + s + L"\r\n";
        // Write wide characters as characters (not bytes)
        f_.write(w.c_str(), static_cast<std::streamsize>(w.size()));
        f_.flush();
    }
private:
    std::wofstream f_;
};

static Logger g_log;

// ============================== Math & Types =================================

struct Vec2i { int x=0, y=0; bool operator==(const Vec2i& o) const { return x==o.x && y==o.y; } };
static inline Vec2i operator+(Vec2i a, Vec2i b){ return {a.x+b.x,a.y+b.y}; }
static inline Vec2i operator-(Vec2i a, Vec2i b){ return {a.x-b.x,a.y-b.y}; }
namespace std {
template<> struct hash<Vec2i> {
    size_t operator()(const Vec2i& v) const noexcept {
        return (uint64_t(uint32_t(v.x))<<32) ^ uint32_t(v.y);
    }
};
}

// ================================ RNG ========================================

// Valid, deterministic default seed (replaces invalid 0xC01onyULL)
static constexpr uint64_t kDefaultSeed = 0xC01DCAFEULL;

class Rng {
public:
    Rng() : eng_(kDefaultSeed) {}
    explicit Rng(uint64_t seed) : eng_(seed ? seed : kDefaultSeed) {}
    int irange(int lo, int hi) { if (lo>hi) std::swap(lo,hi); std::uniform_int_distribution<int> d(lo,hi); return d(eng_); }
    bool chance(double p)      { if (p<=0.0) return false; if (p>=1.0) return true; std::bernoulli_distribution d(p); return d(eng_); }
    double frand(double a=0.0, double b=1.0) { if (a>b) std::swap(a,b); std::uniform_real_distribution<double> d(a,b); return d(eng_); }
private:
    std::mt19937_64 eng_;
};

// =============================== World / Tiles ===============================

enum class TileType : uint8_t { Regolith=0, Rock=1, Ice=2, Crater=3, Sand=4 };

struct Tile {
    TileType type = TileType::Regolith;
    int resource = 0;   // ice/rock pockets
    bool walkable = true;
    uint8_t cost  = 10; // base path cost
};

struct World {
    int W=120, H=80;
    std::vector<Tile> t;
    Rng* rng=nullptr;

    int idx(int x,int y) const { return y*W + x; }
    bool in(int x,int y) const { return x>=0 && y>=0 && x<W && y<H; }
    Tile& at(int x,int y){ return t[idx(x,y)]; }
    const Tile& at(int x,int y) const { return t[idx(x,y)]; }
    void resize(int w,int h){ W=w;H=h;t.assign(W*H,{}); }
    void generate(Rng& r){
        rng=&r;
        for(auto& e:t){ e.type=TileType::Regolith; e.resource=0; e.walkable=true; e.cost=10; }
        // Sand swirls
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            if(r.chance(0.015)){
                int len=r.irange(8,30), dx=(r.irange(0,1)?1:-1), dy=(r.irange(0,1)?1:-1);
                int cx=x,cy=y;
                for(int i=0;i<len;++i){ if(!in(cx,cy)) break; auto& tt=at(cx,cy); tt.type=TileType::Sand; tt.cost=12; cx+=dx; cy+=dy; }
            }
        }
        // Ice pockets
        for(int k=0;k<180;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,2)){ auto& tt=at(X,Y); tt.type=TileType::Ice; tt.walkable=true; tt.cost=14; tt.resource=r.irange(5,20); }
            }
        }
        // Rock clusters
        for(int k=0;k<220;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,5);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-2,2)){ auto& tt=at(X,Y); tt.type=TileType::Rock; tt.walkable=true; tt.cost=16; tt.resource=r.irange(3,12); }
            }
        }
        // Craters
        for(int k=0;k<55;++k){
            int x=r.irange(4,W-5), y=r.irange(4,H-5), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,1)){ auto& tt=at(X,Y); tt.type=TileType::Crater; tt.walkable=false; tt.cost=255; tt.resource=0; }
            }
        }
        // HQ area
        int cx=W/2, cy=H/2;
        for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){
            int X=cx+dx,Y=cy+dy; if(!in(X,Y)) continue; auto& tt=at(X,Y);
            tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
        }
    }
};

// ============================== Pathfinding (A*) =============================

static int manhattan(Vec2i a, Vec2i b){ return std::abs(a.x-b.x)+std::abs(a.y-b.y); }

static bool neighbors4(const World& w, const Vec2i& p, std::array<Vec2i,4>& out, int& n){
    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
    n=0; for(auto d:N){ int nx=p.x+d.x, ny=p.y+d.y; if(!w.in(nx,ny)) continue; if(!w.at(nx,ny).walkable) continue; out[n++]={nx,ny}; } return n>0;
}

static bool findPathAStar(const World& w, Vec2i start, Vec2i goal, std::deque<Vec2i>& out) {
    if(!w.in(start.x,start.y)||!w.in(goal.x,goal.y)) return false;
    if(!w.at(start.x,start.y).walkable||!w.at(goal.x,goal.y).walkable) return false;

    struct Node{ Vec2i p; int g=0,f=0,parent=-1; };
    struct PQ{ int idx; int f; bool operator<(const PQ& o) const { return f>o.f; } };

    auto idxOf=[&](Vec2i p){ return p.y*w.W+p.x; };
    std::vector<Node> nodes; nodes.reserve(w.W*w.H);
    std::vector<int> openIx(w.W*w.H,-1), closedIx(w.W*w.H,-1);
    std::priority_queue<PQ> open;

    Node s; s.p=start; s.g=0; s.f=manhattan(start,goal); s.parent=-1;
    nodes.push_back(s); open.push({0,s.f}); openIx[idxOf(start)]=0;

    std::array<Vec2i,4> neigh; int nc=0;
    while(!open.empty()){
        int ci=open.top().idx; open.pop();
        Node cur=nodes[ci]; Vec2i p=cur.p;
        if(p==goal){
            std::vector<Vec2i> rev; for(int i=ci;i!=-1;i=nodes[i].parent) rev.push_back(nodes[i].p);
            out.clear(); for(int i=(int)rev.size()-1;i>=0;--i) out.push_back(rev[i]);
            if(!out.empty()) out.pop_front(); // remove start tile
            return true;
        }
        closedIx[idxOf(p)]=ci;

        neighbors4(w,p,neigh,nc);
        for(int i=0;i<nc;++i){
            Vec2i np=neigh[i]; int nid=idxOf(np);
            if(closedIx[nid]!=-1) continue;
            int step=w.at(np.x,np.y).cost; int g=cur.g+step;
            int o=openIx[nid];
            if(o==-1){
                Node n; n.p=np; n.g=g; n.f=g+manhattan(np,goal); n.parent=ci;
                o=(int)nodes.size(); nodes.push_back(n); open.push({o,n.f}); openIx[nid]=o;
            } else if(g < nodes[o].g){
                nodes[o].g=g; nodes[o].f=g+manhattan(np,goal); nodes[o].parent=ci; open.push({o,nodes[o].f});
            }
        }
    }
    return false;
}

// ============================== Economy & Entities ===========================

enum class Resource : uint8_t { Metal=0, Ice=1, Oxygen=2, Water=3 };
struct Stockpile { int metal=15, ice=10, oxygen=50, water=40; };

enum class BuildingKind : uint8_t { Solar=0, Habitat=1, OxyGen=2 };
struct BuildingDef {
    BuildingKind kind; Vec2i size;
    int metalCost=0, iceCost=0;
    int powerProd=0, powerCons=0;
    int oxyProd=0,  oxyCons=0;
    int waterProd=0, waterCons=0;
    int housing=0; bool needsDaylight=false;
};
static BuildingDef defSolar()  { return {BuildingKind::Solar,  {2,2},  6,0,   8,0, 0,0, 0,0, 0, true}; }
static BuildingDef defHab()    { return {BuildingKind::Habitat,{3,2}, 12,4,   0,2, 0,2, 0,2, 4, false}; }
static BuildingDef defOxyGen() { return {BuildingKind::OxyGen, {2,2}, 10,6,   2,0, 4,0, 0,0, 0, false}; }

struct Building {
    int id=0; BuildingDef def; Vec2i pos; bool powered=true;
};

struct Colony {
    Stockpile store;
    int powerBalance=0, oxygenBalance=0, waterBalance=0;
    int housing=0, population=0;
};

enum class JobType : uint8_t { None=0, MineRock=1, MineIce=2, Deliver=3, Build=4 };
struct Job { JobType type=JobType::None; Vec2i target{}; int ticks=0; int amount=0; int buildingId=0; };

struct Colonist {
    int id=0; Vec2i tile{0,0}; std::deque<Vec2i> path; Job job; int carryMetal=0; int carryIce=0;
    enum class State: uint8_t { Idle, Moving, Working } state=State::Idle;
};

// ================================ Rendering (GDI) ============================

struct BackBuffer {
    HBITMAP bmp = 0; HDC mem = 0; int w=0, h=0;
    void Create(HDC hdc, int W, int H) {
        Destroy(); w=W; h=H;
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, W, H);
        SelectObject(mem, bmp);
        HBRUSH b = CreateSolidBrush(RGB(0,0,0));
        RECT rc{0,0,W,H}; FillRect(mem, &rc, b); DeleteObject(b);
    }
    void Destroy() { if (mem) { DeleteDC(mem); mem=0; } if (bmp) { DeleteObject(bmp); bmp=0; } w=h=0; }
};

// ================================ Game Impl =================================

static const wchar_t* kWndClass = L"ColonyGame_SingleTU_Win32";
static const wchar_t* kWndTitle = L"Colony Game";

class Game {
public:
    Game(HINSTANCE hInst, const GameOptions& opts)
        : hInst_(hInst), opts_(opts), rng_(opts.seed ? opts.seed : kDefaultSeed) {}

    int Run() {
        if (!CreateMainWindow()) return 3;
        InitWorld();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        util::Timer timer;
        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running_=false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            double dt = timer.Tick();
            if (!paused_) {
                simAcc_ += dt * simSpeed_;
                if (simAcc_ > 0.5) simAcc_ = 0.5;          // avoid spiral after long pause
                while (simAcc_ >= fixedDt_) {
                    Update(fixedDt_);
                    simAcc_ -= fixedDt_;
                }
            }

            Render();
            if (opts_.vsync) Sleep(1);                    // crude vsync-ish
        }
        return 0;
    }

private:
    // ---------------- Window / WndProc ----------------
    static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        Game* self = reinterpret_cast<Game*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(h, m, w, l);
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->WndProc(h, m, w, l);
    }

    bool CreateMainWindow() {
        WNDCLASSW wc{}; wc.hInstance=hInst_; wc.lpfnWndProc=StaticWndProc;
        wc.hCursor=LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=kWndClass;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        if (!RegisterClassW(&wc)) return false;

        if (opts_.fullscreen) {
            // Borderless fullscreen on primary monitor
            hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWndClass, kWndTitle, WS_POPUP,
                0,0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                nullptr, nullptr, hInst_, this);
        } else {
            DWORD style = WS_OVERLAPPEDWINDOW;
            RECT rc{0,0,(LONG)opts_.width,(LONG)opts_.height};
            AdjustWindowRect(&rc, style, FALSE);
            int W = rc.right-rc.left, H = rc.bottom-rc.top;
            hwnd_ = CreateWindowExW(0, kWndClass, kWndTitle, style,
                CW_USEDEFAULT, CW_USEDEFAULT, W, H, nullptr, nullptr, hInst_, this);
        }
        if (!hwnd_) return false;

        // HUD font
        LOGFONTW lf{};
        HDC tmpdc = GetDC(hwnd_);
        lf.lfHeight = -MulDiv(10, GetDeviceCaps(tmpdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd_, tmpdc);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf);
        return true;
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_SIZE: {
            clientW_ = LOWORD(l); clientH_ = HIWORD(l);
            HDC hdc = GetDC(h);
            if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);
            ReleaseDC(h, hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx=GET_X_LPARAM(l), my=GET_Y_LPARAM(l);
            OnLeftClick(mx,my);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            buildMode_ = false; selected_ = std::nullopt;
            return 0;
        }
        case WM_MOUSEWHEEL: {
            short z = GET_WHEEL_DELTA_WPARAM(w);
            if (z>0) zoom_ = util::clamp(zoom_ * 1.1, 0.5, 2.5);
            else     zoom_ = util::clamp(zoom_ / 1.1, 0.5, 2.5);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (w) {
                case VK_ESCAPE:
                    if (buildMode_) { buildMode_=false; selected_.reset(); }
                    else { running_=false; }
                    break;
                case 'P': paused_ = !paused_; break;
                case VK_OEM_PLUS: case VK_ADD: simSpeed_=util::clamp(simSpeed_*1.25,0.25,8.0); break;
                case VK_OEM_MINUS: case VK_SUBTRACT: simSpeed_=util::clamp(simSpeed_/1.25,0.25,8.0); break;
                case '1': selected_=BuildingKind::Solar;  buildMode_=true;  break;
                case '2': selected_=BuildingKind::Habitat;buildMode_=true;  break;
                case '3': selected_=BuildingKind::OxyGen; buildMode_=true;  break;
                case 'G': SpawnColonist(); break;
                case 'B': { auto t=MouseToTile(lastMouse_); Bulldoze(t); } break;
                case VK_LEFT:  keyPan_.x=-1; break;
                case VK_RIGHT: keyPan_.x=+1; break;
                case VK_UP:    keyPan_.y=-1; break;
                case VK_DOWN:  keyPan_.y=+1; break;
                case 'S': SaveGame(); break;
                case 'L': LoadGame(); break;
            }
            return 0;
        }
        case WM_KEYUP: {
            switch (w) {
                case VK_LEFT:  if (keyPan_.x==-1) keyPan_.x=0; break;
                case VK_RIGHT: if (keyPan_.x==+1) keyPan_.x=0; break;
                case VK_UP:    if (keyPan_.y==-1) keyPan_.y=0; break;
                case VK_DOWN:  if (keyPan_.y==+1) keyPan_.y=0; break;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            lastMouse_.x = GET_X_LPARAM(l);
            lastMouse_.y = GET_Y_LPARAM(l);
            return 0;
        }
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    // ---------------- World / Sim init ----------------
    void InitWorld() {
        // Log
        std::wstring logDir = util::Widen(opts_.saveDir);
        if (!logDir.empty()) {
            std::wstring logs = util::JoinPath(logDir.substr(0, logDir.find(L"\\Saves")), L"Logs");
            util::EnsureDir(logs);
            g_log.Open(util::JoinPath(logs, L"Game-" + util::NowStampCompact() + L".log"));
        }
        g_log.Line(L"Game init…");

        tileSize_ = 24;
        world_.resize(120, 80);
        world_.generate(rng_);

        hq_ = { world_.W/2, world_.H/2 };
        TryPlaceImmediate(BuildingKind::Solar,  hq_ + Vec2i{3,-2});
        TryPlaceImmediate(BuildingKind::Habitat,hq_ + Vec2i{3, 0});
        TryPlaceImmediate(BuildingKind::OxyGen, hq_ + Vec2i{0, 3});

        // Center camera
        camera_.x = (hq_.x*tileSize_) - clientW_/2;
        camera_.y = (hq_.y*tileSize_) - clientH_/2;

        SpawnColonist();
        Banner(L"Welcome to Mars — profile: " + util::Widen(opts_.profile));
    }

    void SpawnColonist() {
        Colonist c; c.id = nextColonistId_++; c.tile = hq_; colonists_.push_back(c);
        Banner(L"Colonist arrived");
    }

    // ---------------- Input helpers ------------------
    Vec2i MouseToTile(POINT p) const {
        int wx = int(camera_.x + p.x/zoom_);
        int wy = int(camera_.y + p.y/zoom_);
        return { wx / tileSize_, wy / tileSize_ };
    }
    void OnLeftClick(int mx, int my) {
        POINT p{mx,my};
        if (buildMode_ && selected_.has_value()) {
            Vec2i t = MouseToTile(p);
            TryQueueBuild(*selected_, t);
            buildMode_=false; selected_.reset();
            return;
        }
    }

    // ---------------- Build placement ----------------
    BuildingDef Def(BuildingKind k) {
        switch(k) {
            case BuildingKind::Solar:  return defSolar();
            case BuildingKind::Habitat:return defHab();
            case BuildingKind::OxyGen: return defOxyGen();
        }
        return defSolar();
    }
    bool CheckFootprint(const BuildingDef& d, Vec2i topLeft) {
        for(int dy=0;dy<d.size.y;++dy) for(int dx=0;dx<d.size.x;++dx) {
            int x=topLeft.x+dx, y=topLeft.y+dy;
            if(!world_.in(x,y)) return false;
            const Tile& t = world_.at(x,y);
            if(!t.walkable || t.type==TileType::Crater) return false;
        }
        return true;
    }
    void Bulldoze(Vec2i t) {
        if(!world_.in(t.x,t.y)) return;
        auto& tt=world_.at(t.x,t.y);
        tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
    }
    bool TryQueueBuild(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) { Banner(L"Invalid location"); return false; }
        if (colony_.store.metal < d.metalCost || colony_.store.ice < d.iceCost) { Banner(L"Not enough resources"); return false; }
        pendingBuild_ = Building{ nextBuildingId_++, d, topLeft, true };
        Banner(L"Construction queued: " + NameOf(k));
        return true;
    }
    void TryPlaceImmediate(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) return;
        buildings_.push_back(Building{ nextBuildingId_++, d, topLeft, true });
    }

    // ---------------- Update loop --------------------
    void Update(double dt) {
        // Camera pan
        const double pan=300.0;
        camera_.x += keyPan_.x * pan * dt;
        camera_.y += keyPan_.y * pan * dt;

        // Day/night
        dayTime_ += dt*0.02;
        if (dayTime_>=1.0) dayTime_ -= 1.0;

        EconomyTick();
        AITick();
    }
    void EconomyTick() {
        colony_.powerBalance = colony_.oxygenBalance = colony_.waterBalance = 0;
        colony_.housing = 0;
        bool daylight = (dayTime_>0.1 && dayTime_<0.9);
        for(auto& b:buildings_) {
            b.powered = true;
            if (b.def.needsDaylight && !daylight) { /* no solar output */ }
            else colony_.powerBalance += b.def.powerProd;
            colony_.powerBalance -= b.def.powerCons;

            colony_.oxygenBalance += b.def.oxyProd;
            colony_.oxygenBalance -= b.def.oxyCons;
            colony_.waterBalance  += b.def.waterProd;
            colony_.waterBalance  -= b.def.waterCons;
            colony_.housing       += b.def.housing;
        }
        colony_.store.oxygen = std::max(0, colony_.store.oxygen + colony_.oxygenBalance);
        colony_.store.water  = std::max(0, colony_.store.water  + colony_.waterBalance);
        int people = (int)colonists_.size();
        if (people>0) {
            colony_.store.oxygen = std::max(0, colony_.store.oxygen - people);
            colony_.store.water  = std::max(0, colony_.store.water  - people);
        }
        colony_.population=people;
    }
    void AITick() {
        for (auto& c:colonists_) {
            switch (c.state) {
                case Colonist::State::Idle:    AIIdle(c); break;
                case Colonist::State::Moving:  AIMove(c); break;
                case Colonist::State::Working: AIWork(c); break;
            }
        }
    }
    void AIIdle(Colonist& c) {
        if (pendingBuild_.has_value()) {
            // Go adjacent to footprint
            std::vector<Vec2i> opts;
            for(int dy=0;dy<pendingBuild_->def.size.y;++dy)
                for(int dx=0;dx<pendingBuild_->def.size.x;++dx) {
                    Vec2i p = pendingBuild_->pos + Vec2i{dx,dy};
                    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
                    for(auto d:N){ Vec2i n=p+d; if(world_.in(n.x,n.y)&&world_.at(n.x,n.y).walkable) opts.push_back(n); }
                }
            if(!opts.empty()){
                Vec2i pick = opts[rng_.irange(0,(int)opts.size()-1)];
                std::deque<Vec2i> path;
                if (findPathAStar(world_, c.tile, pick, path)) {
                    c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Build,pendingBuild_->pos,18,0,pendingBuild_->id};
                    return;
                }
            }
        }
        if (colony_.store.oxygen < 40) if (TryAssignMining(c, TileType::Ice)) return;
        if (TryAssignMining(c, TileType::Rock)) return;
        // wander to HQ
        if (c.tile != hq_) {
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,0,0};}
        }
    }
    bool TryAssignMining(Colonist& c, TileType tt) {
        int bestD = INT32_MAX; Vec2i best{-1,-1};
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x){
            const Tile& t=world_.at(x,y);
            if (t.type==tt && t.resource>0 && t.walkable) {
                int d=manhattan(c.tile,{x,y});
                if(d<bestD){bestD=d; best={x,y};}
            }
        }
        if (best.x>=0) {
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, best, path)) {
                c.path=std::move(path); c.state=Colonist::State::Moving;
                c.job={ (tt==TileType::Ice)?JobType::MineIce:JobType::MineRock, best, 18, 0, 0 };
                return true;
            }
        }
        return false;
    }
    void AIMove(Colonist& c) {
        moveAcc_ += fixedDt_;
        const double step=0.12;
        if (moveAcc_>=step && !c.path.empty()) {
            c.tile=c.path.front(); c.path.pop_front();
            moveAcc_-=step;
            if (c.path.empty()) { c.state=Colonist::State::Working; c.job.ticks=18; }
        }
    }
    void AIWork(Colonist& c) {
        if (c.job.ticks>0) { --c.job.ticks; return; }
        if (c.job.type==JobType::MineIce || c.job.type==JobType::MineRock) {
            Tile& t=world_.at(c.job.target.x,c.job.target.y);
            int mined = std::min(3, t.resource);
            if (mined<=0){ c.state=Colonist::State::Idle; return; }
            t.resource -= mined;
            if (c.job.type==JobType::MineIce) c.carryIce += mined; else c.carryMetal += mined;
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,mined,0};}
            else c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Deliver) {
            colony_.store.metal += c.carryMetal; c.carryMetal=0;
            colony_.store.ice   += c.carryIce;   c.carryIce=0;
            c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Build) {
            if (pendingBuild_.has_value() && pendingBuild_->id==c.job.buildingId) {
                if (colony_.store.metal >= pendingBuild_->def.metalCost &&
                    colony_.store.ice   >= pendingBuild_->def.iceCost) {
                    colony_.store.metal -= pendingBuild_->def.metalCost;
                    colony_.store.ice   -= pendingBuild_->def.iceCost;
                    buildings_.push_back(*pendingBuild_);
                    pendingBuild_.reset();
                }
            }
            c.state=Colonist::State::Idle;
        } else {
            c.state=Colonist::State::Idle;
        }
    }

    // ---------------- Save / Load --------------------
    void SaveGame() {
        if (opts_.saveDir.empty()) { Banner(L"Save dir not set"); return; }
        std::wstring saveDir = util::Widen(opts_.saveDir);
        util::EnsureDir(saveDir);
        std::wstring file = util::JoinPath(saveDir, util::Widen(opts_.profile) + L".save");
        std::ofstream out(file, std::ios::out | std::ios::trunc);
        if (!out) { Banner(L"Save failed"); return; }
        out << "MCS_SAVE v1\n";
        out << "seed " << opts_.seed << "\n";
        out << "world " << world_.W << " " << world_.H << "\n";
        out << "hq " << hq_.x << " " << hq_.y << "\n";
        out << "store " << colony_.store.metal << " " << colony_.store.ice << " "
            << colony_.store.oxygen << " " << colony_.store.water << "\n";
        out << "buildings " << buildings_.size() << "\n";
        for (auto& b : buildings_) {
            out << int(b.def.kind) << " " << b.pos.x << " " << b.pos.y << "\n";
        }
        if (pendingBuild_.has_value()) {
            out << "pending 1 " << int(pendingBuild_->def.kind) << " "
                << pendingBuild_->pos.x << " " << pendingBuild_->pos.y << " " << pendingBuild_->id << "\n";
        } else out << "pending 0\n";
        out << "colonists " << colonists_.size() << "\n";
        for (auto& c : colonists_) {
            out << c.id << " " << c.tile.x << " " << c.tile.y << "\n";
        }
        Banner(L"Game saved");
    }

    void LoadGame() {
        if (opts_.saveDir.empty()) { Banner(L"Save dir not set"); return; }
        std::wstring file = util::JoinPath(util::Widen(opts_.saveDir), util::Widen(opts_.profile) + L".save");
        std::ifstream in(file);
        if (!in) { Banner(L"No save"); return; }

        std::string header; in >> header; if (header != "MCS_SAVE") { Banner(L"Bad save"); return; }
        std::string tag; in >> tag; /* v1 */

        uint64_t seedIn=0; in >> tag; if (tag!="seed") { Banner(L"Load fail: seed"); return; } in >> seedIn;
        int W,H; in >> tag; if (tag!="world") { Banner(L"Load fail: world"); return; } in >> W >> H; world_.resize(W,H); world_.generate(rng_);
        in >> tag; if (tag!="hq") { Banner(L"Load fail: hq"); return; } in >> hq_.x >> hq_.y;
        in >> tag; if (tag!="store") { Banner(L"Load fail: store"); return; }
        in >> colony_.store.metal >> colony_.store.ice >> colony_.store.oxygen >> colony_.store.water;

        in >> tag; if (tag!="buildings") { Banner(L"Load fail: buildings"); return; }
        size_t bc; in >> bc; buildings_.clear();
        for (size_t i=0;i<bc;++i) {
            int kind,x,y; in >> kind >> x >> y;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            buildings_.push_back(Building{ nextBuildingId_++, def, {x,y}, true});
        }

        in >> tag; if (tag!="pending") { Banner(L"Load fail: pending"); return; }
        int hasPending; in >> hasPending;
        if (hasPending==1) {
            int kind,x,y,id; in >> kind >> x >> y >> id;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            pendingBuild_ = Building{id, def, {x,y}, true};
        } else pendingBuild_.reset();

        in >> tag; if (tag!="colonists") { Banner(L"Load fail: colonists"); return; }
        size_t cc; in >> cc; colonists_.clear();
        for (size_t i=0;i<cc;++i) {
            Colonist c; in >> c.id >> c.tile.x >> c.tile.y;
            colonists_.push_back(c);
            nextColonistId_ = std::max(nextColonistId_, c.id+1);
        }
        Banner(L"Game loaded");
    }

    // ---------------- Rendering ----------------------
    void Render() {
        HDC hdc = GetDC(hwnd_);
        if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);

        // Mars-ish sky based on dayTime_
        double daylight = std::cos((dayTime_-0.5)*3.14159*2.0)*0.5+0.5;
        int R=int(120+70*daylight), G=int(40+30*daylight), B=int(35+25*daylight);
        HBRUSH sky = CreateSolidBrush(RGB(R,G,B));
        RECT full{0,0,clientW_,clientH_}; FillRect(back_.mem, &full, sky); DeleteObject(sky);

        DrawWorld();
        DrawBuildings();
        DrawColonists();
        if (buildMode_ && selected_.has_value()) DrawPlacement(*selected_);
        DrawHQ();
        DrawHUD();

        BitBlt(hdc, 0,0, clientW_,clientH_, back_.mem, 0,0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);
    }

    void DrawWorld() {
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x) {
            const Tile& t=world_.at(x,y);
            COLORREF c = RGB(139,85,70);
            switch(t.type){
                case TileType::Regolith: c=RGB(139,85,70); break;
                case TileType::Sand:     c=RGB(168,120,85); break;
                case TileType::Ice:      c=RGB(120,170,200); break;
                case TileType::Rock:     c=RGB(100,100,110); break;
                case TileType::Crater:   c=RGB(40,40,45); break;
            }
            DrawCell(x,y,c);
            // subtle grid outline
            HPEN pen=CreatePen(PS_SOLID,1,RGB(0,0,0)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
            RECT rc = TileRect(x,y); MoveToEx(back_.mem, rc.left, rc.top, nullptr);
            LineTo(back_.mem, rc.right, rc.top); LineTo(back_.mem, rc.right, rc.bottom); LineTo(back_.mem, rc.left, rc.bottom); LineTo(back_.mem, rc.left, rc.top);
            SelectObject(back_.mem, old); DeleteObject(pen);
        }
    }
    void DrawBuildings() {
        for(auto& b:buildings_){
            COLORREF col = (b.def.kind==BuildingKind::Solar)?RGB(60,120,200):
                           (b.def.kind==BuildingKind::Habitat)?RGB(200,160,80):RGB(90,200,140);
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(col); FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        if (pendingBuild_.has_value()) {
            auto& b=*pendingBuild_;
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(RGB(255,255,255));
            FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
    }
    void DrawColonists() {
        for(auto& c:colonists_) {
            RECT rc = TileRect(c.tile.x,c.tile.y);
            HBRUSH br=CreateSolidBrush(RGB(240,90,70)); FillRect(back_.mem,&rc,br); DeleteObject(br);

            if (!c.path.empty()) {
                HPEN pen=CreatePen(PS_SOLID,2,RGB(30,220,255)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
                Vec2i prev=c.tile;
                for(auto p:c.path){
                    RECT a=TileRect(prev.x,prev.y), b=TileRect(p.x,p.y);
                    int ax=(a.left+a.right)/2, ay=(a.top+a.bottom)/2;
                    int bx=(b.left+b.right)/2, by=(b.top+b.bottom)/2;
                    MoveToEx(back_.mem, ax, ay, nullptr); LineTo(back_.mem, bx, by);
                    prev=p;
                }
                SelectObject(back_.mem, old); DeleteObject(pen);
            }
        }
    }
    void DrawPlacement(BuildingKind k) {
        Vec2i t = MouseToTile(lastMouse_);
        auto d = Def(k);
        bool ok = CheckFootprint(d,t);
        RECT rc = TileRect(t.x,t.y);
        rc.right  = rc.left + int(d.size.x * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(d.size.y * tileSize_ * zoom_);
        HBRUSH br = CreateSolidBrush(ok?RGB(100,255,100):RGB(255,80,80));
        FillRect(back_.mem,&rc,br); DeleteObject(br);
        FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        std::wstringstream tip;
        tip << NameOf(k) << L"  M:" << d.metalCost << L" I:" << d.iceCost;
        DrawTooltip(lastMouse_.x+14, lastMouse_.y+14, tip.str());
    }
    void DrawHQ() {
        RECT rc = TileRect(hq_.x,hq_.y);
        rc.right  = rc.left + int(2 * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(2 * tileSize_ * zoom_);
        HBRUSH br=CreateSolidBrush(RGB(200,80,120)); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    void DrawHUD() {
        int pad=8, w=560, h=116;
        RECT hud{pad,pad,pad+w,pad+h};
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&hud,bg); DeleteObject(bg);
        FrameRect(back_.mem,&hud,(HBRUSH)GetStockObject(BLACK_BRUSH));

        HFONT oldFont = (HFONT)SelectObject(back_.mem, font_);
        SetBkMode(back_.mem, TRANSPARENT);
        SetTextColor(back_.mem, RGB(230,230,240));

        int x=hud.left+8, y=hud.top+6;
        std::wstringstream l1; l1<<L"Time "<<std::fixed<<std::setprecision(2)<<dayTime_<<L"   x"<<std::setprecision(2)<<simSpeed_<<(paused_?L"  [PAUSED]":L"");
        DrawTextLine(x,y,l1.str()); y+=16;

        std::wstringstream r1; r1<<L"Metal "<<colony_.store.metal<<L"   Ice "<<colony_.store.ice<<L"   O2 "<<colony_.store.oxygen<<L"   H2O "<<colony_.store.water;
        DrawTextLine(x,y,r1.str()); y+=16;

        std::wstringstream r2; r2<<L"Power "<<colony_.powerBalance<<L"   O2 "<<colony_.oxygenBalance<<L"   H2O "<<colony_.waterBalance<<L"   Pop "<<colony_.population<<L"/"<<colony_.housing;
        DrawTextLine(x,y,r2.str()); y+=16;

        std::wstring sel = selected_.has_value()? NameOf(*selected_) : L"None";
        DrawTextLine(x,y, L"Build: "+sel); y+=16;

        SetTextColor(back_.mem, RGB(255,128,64));
        DrawTextLine(x,y, L"1=Solar  2=Hab  3=O2Gen   LMB place  RMB cancel  G colonist  S/L save/load  P pause  +/- speed  Arrows pan");

        SelectObject(back_.mem, oldFont);

        if (!banner_.empty() && bannerTime_>0.0) {
            int bw = (int)banner_.size()*8+24; int bh=24;
            RECT b{ (clientW_-bw)/2, clientH_-bh-12, (clientW_+bw)/2, clientH_-12 };
            HBRUSH bb=CreateSolidBrush(RGB(30,30,35)); FillRect(back_.mem,&b,bb); DeleteObject(bb);
            FrameRect(back_.mem, &b, (HBRUSH)GetStockObject(BLACK_BRUSH));
            HFONT of=(HFONT)SelectObject(back_.mem,font_);
            SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(255,255,255));
            RECT trc=b; trc.left+=12; trc.top+=4; DrawTextW(back_.mem, banner_.c_str(), -1, &trc, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(back_.mem,of);
            bannerTime_ -= 0.016;
            if (bannerTime_<=0.0) banner_.clear();
        }
    }

    void DrawTextLine(int x, int y, const std::wstring& s) {
        RECT rc{ x,y,x+1000,y+16 };
        DrawTextW(back_.mem, s.c_str(), -1, &rc, DT_LEFT|DT_TOP|DT_SINGLELINE);
    }

    void DrawTooltip(int x, int y, const std::wstring& text) {
        RECT rc{ x,y,x+(int)text.size()*8+8, y+20 };
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&rc,bg); DeleteObject(bg);
        FrameRect(back_.mem,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HFONT of=(HFONT)SelectObject(back_.mem,font_);
        SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(230,230,240));
        RECT t=rc; t.left+=4; t.top+=2; DrawTextW(back_.mem, text.c_str(), -1, &t, DT_LEFT|DT_TOP|DT_SINGLELINE);
        SelectObject(back_.mem,of);
    }

    RECT TileRect(int tx,int ty) const {
        int px = int((tx*tileSize_ - camera_.x) * zoom_);
        int py = int((ty*tileSize_ - camera_.y) * zoom_);
        int s  = int(tileSize_ * zoom_);
        RECT rc{px,py,px+s,py+s};
        return rc;
    }
    void DrawCell(int x,int y, COLORREF c) {
        RECT rc = TileRect(x,y);
        HBRUSH br=CreateSolidBrush(c); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    std::wstring NameOf(BuildingKind k) {
        switch(k){
            case BuildingKind::Solar:  return L"Solar Panel";
            case BuildingKind::Habitat:return L"Habitat";
            case BuildingKind::OxyGen: return L"Oxygen Generator";
        }
        return L"?";
    }
    void Banner(const std::wstring& s) { banner_ = s; bannerTime_ = 3.0; }

private:
    // Win
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    BackBuffer back_;
    HFONT font_ = nullptr;
    int clientW_=1280, clientH_=720;

    // Camera
    struct { double x=0,y=0; } camera_;
    double zoom_=1.0;

    // Options
    GameOptions opts_;

    // World
    World world_;
    Rng   rng_;
    int   tileSize_=24;
    Vec2i hq_{0,0};
    std::vector<Building> buildings_;
    std::optional<Building> pendingBuild_;
    int nextBuildingId_=1;

    std::vector<Colonist> colonists_;
    int nextColonistId_=1;

    Colony colony_;

    // Sim
    bool running_=true, paused_=false;
    double simSpeed_=1.0;
    const double fixedDt_=1.0/60.0;
    double simAcc_=0.0, moveAcc_=0.0, dayTime_=0.25;

    // Input state
    Vec2i keyPan_{0,0};
    bool buildMode_=false; std::optional<BuildingKind> selected_;
    POINT lastMouse_{};

    // Banner
    std::wstring banner_; double bannerTime_=0.0;
};

// ============================ Public Entry Point =============================

int RunColonyGame(const GameOptions& opts) {
    // Win boilerplate
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    Game game(hInst, opts);
    int rc = game.Run();

    CoUninitialize();
    return rc;
}

// ============================================================================
//                            EXPANSION POINTS
// ============================================================================
//
// You can paste more systems here without touching any other file.
//
// Suggested modules to reach/approach ~3,000 LOC:
//  1) Research & Tech Tree:
//      - Research building (consumes power/O2).
//      - Unlocks: Battery (stores power at day, releases at night), Water Extractor,
//        Refinery (regolith→metal), Greenhouse (water→food, grants morale bonus).
//      - UI panel: queue research, progress bars.
//
//  2) Vehicles & Logistics:
//      - Rover entity (faster hauling; pathfinding same API).
//      - Stockpile nodes & hauling tasks (priority queue).
//      - Roads (lower tile cost), buildable by colonists.
//
//  3) Events & Weather:
//      - Dust storms (reduce solar output, slow movement).
//      - Meteor strikes (spawn craters, damage buildings).
//      - Random “anomalies” that grant resources or tech points.
//
//  4) Colonist Simulation:
//      - Traits (Engineer/Scientist/Miner).
//      - Needs (morale, fatigue) that influence productivity.
//      - Homes/jobs assignment; vacancy checks against Habitat housing.
//
//  5) UI Panels:
//      - Build menu, Jobs panel, Resources panel, Messages log.
//      - Tooltips for buildings with production/consumption deltas.
//
//  6) Save/Load v2:
//      - Backward-compatible format; chunked sections with checksums.
//      - Autosave every N minutes.
//
//  7) Screenshot utility:
//      - BitBlt backbuffer to a BMP in %LOCALAPPDATA%\MarsColonySim\Screenshots.
//
// ============================================================================
