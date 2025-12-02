#include "Hydrology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>
#include <type_traits>

// ================================================================
// Build toggles (safe defaults). You can override in CMake:
//   add_compile_definitions(HYDRO_USE_DINF=1)
// ================================================================
#ifndef HYDRO_USE_DINF
#define HYDRO_USE_DINF 1   // 0=D8 only, 1=enable D∞ (Tarboton) recipients with angle-split
#endif

#ifndef HYDRO_COAST_DECAY_CELLS
#define HYDRO_COAST_DECAY_CELLS 64.0f // scale length for coast-proximity evaporation boost
#endif

#ifndef HYDRO_MIN_SLOPE
#define HYDRO_MIN_SLOPE 1e-4f // slope floor to avoid zero in stream-power
#endif

#ifndef HYDRO_EPS_SUPPLY
#define HYDRO_EPS_SUPPLY 1e-3f // base trickle so channels connect even in deserts
#endif

// If you want to export/diagnose a pure D8 direction map from this TU,
// set HYDRO_EXPOSE_D8_HELPERS=1. When left at 0 (default), the helper
// function isn't compiled at all, which keeps /WX clean (no C4505).
#ifndef HYDRO_EXPOSE_D8_HELPERS
#define HYDRO_EXPOSE_D8_HELPERS 0
#endif

namespace cg {

// ---------------------------------------------------------------
// Member detection helpers (compat with renamed params)
// ---------------------------------------------------------------
template<typename T, typename = void> struct has_tempLatGradient   : std::false_type {};
template<typename T> struct has_tempLatGradient<T,   std::void_t<decltype(std::declval<const T&>().tempLatGradient)>>   : std::true_type {};

template<typename T, typename = void> struct has_tempLapseRate     : std::false_type {};
template<typename T> struct has_tempLapseRate<T,     std::void_t<decltype(std::declval<const T&>().tempLapseRate)>>     : std::true_type {};

template<typename T, typename = void> struct has_tempSeaLevel      : std::false_type {};
template<typename T> struct has_tempSeaLevel<T,      std::void_t<decltype(std::declval<const T&>().tempSeaLevel)>>      : std::true_type {};

template<typename T, typename = void> struct has_climateSeaLevel   : std::false_type {};
template<typename T> struct has_climateSeaLevel<T,   std::void_t<decltype(std::declval<const T&>().seaLevel)>>          : std::true_type {};

template<typename T, typename = void> struct has_hydroSeaLevel     : std::false_type {};
template<typename T> struct has_hydroSeaLevel<T,     std::void_t<decltype(std::declval<const T&>().seaLevel)>>          : std::true_type {};

template<typename T, typename = void> struct has_hydroSeaLevelM    : std::false_type {};
template<typename T> struct has_hydroSeaLevelM<T,    std::void_t<decltype(std::declval<const T&>().seaLevelMeters)>>    : std::true_type {};

template<typename T, typename = void> struct has_incision_m        : std::false_type {};
template<typename T> struct has_incision_m<T,        std::void_t<decltype(std::declval<const T&>().incision_m)>>        : std::true_type {};

template<typename T, typename = void> struct has_incision_m_exp    : std::false_type {};
template<typename T> struct has_incision_m_exp<T,    std::void_t<decltype(std::declval<const T&>().incisionExp_m)>>     : std::true_type {};

template<typename T, typename = void> struct has_incision_n        : std::false_type {};
template<typename T> struct has_incision_n<T,        std::void_t<decltype(std::declval<const T&>().incision_n)>>        : std::true_type {};

template<typename T, typename = void> struct has_incision_n_exp    : std::false_type {};
template<typename T> struct has_incision_n_exp<T,    std::void_t<decltype(std::declval<const T&>().incisionExp_n)>>     : std::true_type {};

template<typename T, typename = void> struct has_smoothIterations  : std::false_type {};
template<typename T> struct has_smoothIterations<T,  std::void_t<decltype(std::declval<const T&>().smoothIterations)>>  : std::true_type {};

template<typename T, typename = void> struct has_smoothIters       : std::false_type {};
template<typename T> struct has_smoothIters<T,       std::void_t<decltype(std::declval<const T&>().smoothIters)>>       : std::true_type {};

// Optional alternates commonly seen in erosion codebases
template<typename T, typename = void> struct has_streamPower_m     : std::false_type {};
template<typename T> struct has_streamPower_m<T, std::void_t<decltype(std::declval<const T&>().streamPower_m)>>         : std::true_type {};

template<typename T, typename = void> struct has_streamPower_n     : std::false_type {};
template<typename T> struct has_streamPower_n<T, std::void_t<decltype(std::declval<const T&>().streamPower_n)>>         : std::true_type {};

template<typename T>
static float tempLatGradient_of(const T& c) {
    if constexpr (has_tempLatGradient<T>::value) return static_cast<float>(c.tempLatGradient);
    else                                         return static_cast<float>(c.latGradientKPerDeg);
}
template<typename T>
static float tempLapseRate_of(const T& c) {
    if constexpr (has_tempLapseRate<T>::value) return static_cast<float>(c.tempLapseRate);
    else                                       return static_cast<float>(c.lapseRateKPerKm);
}
template<typename T>
static float tempSeaLevel_of(const T& c) {
    if constexpr (has_tempSeaLevel<T>::value) return static_cast<float>(c.tempSeaLevel);
    else                                       return static_cast<float>(c.seaLevelTempK);
}
template<typename T>
static float seaLevelClimate_of(const T& c) {
    if constexpr (has_climateSeaLevel<T>::value) return static_cast<float>(c.seaLevel);
    else                                          return 0.0f; // fallback if climate sea-level not modeled
}
template<typename T>
static float seaLevel_of(const T& h) {
    if constexpr (has_hydroSeaLevel<T>::value)  return static_cast<float>(h.seaLevel);
    else if constexpr (has_hydroSeaLevelM<T>::value) return static_cast<float>(h.seaLevelMeters);
    else return 0.0f;
}
template<typename T>
static float incision_m_of(const T& h) {
    if constexpr (has_incision_m<T>::value)         return static_cast<float>(h.incision_m);
    else if constexpr (has_incision_m_exp<T>::value)return static_cast<float>(h.incisionExp_m);
    else if constexpr (has_streamPower_m<T>::value) return static_cast<float>(h.streamPower_m);
    else                                            return 0.5f; // typical default for stream-power m
}
template<typename T>
static float incision_n_of(const T& h) {
    if constexpr (has_incision_n<T>::value)         return static_cast<float>(h.incision_n);
    else if constexpr (has_incision_n_exp<T>::value)return static_cast<float>(h.incisionExp_n);
    else if constexpr (has_streamPower_n<T>::value) return static_cast<float>(h.streamPower_n);
    else                                            return 1.0f; // typical default for stream-power n
}
template<typename T>
static int smoothIterations_of(const T& h) {
    if constexpr (has_smoothIterations<T>::value) return h.smoothIterations;
    else                                           return h.smoothIters;
}

// ---------------------------------------------------------------
// Small utilities and constants
// ---------------------------------------------------------------
static inline int idx(int x, int y, int W) { return y * W + x; }

static const int   DX8[8]   = { +1,+1, 0,-1,-1,-1, 0,+1 };
static const int   DY8[8]   = {  0,-1,-1,-1, 0,+1,+1,+1 }; // E,NE,N,NW,W,SW,S,SE  (CCW)
static const float DIST8[8] = { 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };

template <class T>
static inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline float cross2(float ax,float ay,float bx,float by){ return ax*by - ay*bx; }
static inline bool inBounds(int x,int y,int W,int H){ return (x>=0 && x<W && y>=0 && y<H); }

// ---------------------------------------------------------------
// 1) Temperature model (sea-level base, lapse with elevation, N/S gradient)
// ---------------------------------------------------------------
static HeightField computeTemperature(const HeightField& H, const ClimateParams& C)
{
    HeightField T(H.w, H.h);
    const float cGrad   = tempLatGradient_of(C);
    const float cLapse  = tempLapseRate_of(C);
    const float cT0     = tempSeaLevel_of(C);
    const float seaEL   = seaLevelClimate_of(C);

    for (int y=0; y<H.h; ++y) {
        // Positive y is "south" in our grid; apply a simple north-south gradient
        float latTerm = cGrad * float(y - H.h/2);
        for (int x=0; x<H.w; ++x) {
            float z = H.at(x,y);
            float elev = z - seaEL;
            float lapse = cLapse * std::max(0.0f, elev);
            T.at(x,y) = cT0 + latTerm - lapse;
        }
    }
    return T;
}

// ---------------------------------------------------------------
// 2) Coast proximity (for evaporation boost): BFS distance (4-neigh)
// ---------------------------------------------------------------
static std::vector<int> distanceToCoast(const HeightField& H, float seaLevel)
{
    const int W=H.w, HH=H.h, N=W*HH;
    std::vector<int> dist(N, std::numeric_limits<int>::max());
    std::queue<int> q;

    // Initialize queue with ocean cells and their immediate land neighbors
    for (int y=0; y<HH; ++y){
        for (int x=0; x<W; ++x){
            float z = H.at(x,y);
            bool ocean = (z <= seaLevel);
            if (ocean) {
                int id = idx(x,y,W);
                dist[id] = 0;
                q.push(id);
            }
        }
    }

    const int d4x[4] = {+1,-1,0,0};
    const int d4y[4] = {0,0,+1,-1};

    while(!q.empty()){
        int i = q.front(); q.pop();
        int x = i % W, y = i / W;
        int di = dist[i];
        for (int k=0;k<4;++k){
            int nx=x+d4x[k], ny=y+d4y[k];
            if (!inBounds(nx,ny,W,HH)) continue;
            int j = idx(nx,ny,W);
            if (dist[j] > di + 1) {
                dist[j] = di + 1;
                q.push(j);
            }
        }
    }
    return dist;
}

// ---------------------------------------------------------------
// 3) Orographic rainfall with multi-wind support
//
// Approach inspired by Red Blob's mapgen4 notes:
//  - Traverse along prevailing wind; track humidity with coast/evaporation source
//  - Orographic uplift → rain on windward slopes; lee → rain shadow
//  - Add a tiny "regular rain" background to avoid total zeros
//  - Combine multiple cardinal wind passes using weights from (windX,windY)
//
// References: Red Blob's mapgen4 (rainfall blog & demo). :contentReference[oaicite:5]{index=5}
// ---------------------------------------------------------------
struct WindDir { int sx, stepx; int sy, stepy; float weight; }; // scan start and step based on sign

static void addPrecipOnePass(const HeightField& H,
                             const std::vector<int>& coastDist,
                             const ClimateParams& C,
                             int axisMajor,  // 0: x-major scan, 1: y-major scan
                             int sx, int stepx, // x start, +1 or -1
                             int sy, int stepy, // y start, +1 or -1
                             float weight,
                             HeightField& Paccum)
{
    const int W=H.w, HH=H.h;
    if (weight <= 0.0f) return;

    // Normalize wind magnitude to scale advection
    float windMag = std::sqrt(C.windX*C.windX + C.windY*C.windY);
    float advect = (windMag > 1e-6f) ? (0.5f + 0.5f * (windMag / (std::fabs(C.windX)+std::fabs(C.windY)+1e-3f))) : 0.75f;

    // Coast proximity factor: closer to coast → stronger evaporation source
    auto coastBoost = [&](int x,int y){
        int d = coastDist[idx(x,y,W)];
        float s = d / HYDRO_COAST_DECAY_CELLS; // cells → scale length
        return 1.0f / (1.0f + s);               // 1 at coast, decays inland
    };

    if (axisMajor == 0) { // x-major: sweep rows along x
        for (int y=0; y<HH; ++y) {
            float humidity = 0.0f;
            float prevh = H.at(sx,y);
            for (int t=0; t<W; ++t) {
                int x = sx + t*stepx; if (x < 0 || x >= W) break;
                float h = H.at(x,y);

                // Evaporation source scaled by coast proximity and submergence
                float nearCoast = coastBoost(x,y);
                float evap = C.baseEvaporation * nearCoast * clamp(1.0f - (h - seaLevelClimate_of(C)) * 0.01f, 0.0f, 1.0f);
                humidity = clamp(humidity + evap, 0.0f, 4.0f);

                // Orographic uplift on upslope
                float dh = h - prevh;
                if (dh > 0.0f) {
                    float rain = C.orographicFactor * dh * advect;
                    rain = std::min(rain, humidity);
                    humidity -= rain;
                    Paccum.at(x,y) += weight * rain;
                } else if (dh < 0.0f) {
                    // Lee drying (rain shadow)
                    humidity *= (1.0f - clamp(C.rainShadow * (-dh) * 0.01f, 0.0f, 0.95f));
                }

                // Background regular rain + re-evaporation loop
                float background = 0.1f * C.baseEvaporation;
                float br = std::min(background, humidity*0.1f);
                humidity -= br;
                Paccum.at(x,y) += weight * br;

                prevh = h;
            }
        }
    } else { // y-major: sweep columns along y
        for (int x=0; x<W; ++x) {
            float humidity = 0.0f;
            float prevh = H.at(x,sy);
            for (int t=0; t<HH; ++t) {
                int y = sy + t*stepy; if (y < 0 || y >= HH) break;
                float h = H.at(x,y);

                float nearCoast = coastBoost(x,y);
                float evap = C.baseEvaporation * nearCoast * clamp(1.0f - (h - seaLevelClimate_of(C)) * 0.01f, 0.0f, 1.0f);
                humidity = clamp(humidity + evap, 0.0f, 4.0f);

                float dh = h - prevh;
                if (dh > 0.0f) {
                    float rain = C.orographicFactor * dh * advect;
                    rain = std::min(rain, humidity);
                    humidity -= rain;
                    Paccum.at(x,y) += weight * rain;
                } else if (dh < 0.0f) {
                    humidity *= (1.0f - clamp(C.rainShadow * (-dh) * 0.01f, 0.0f, 0.95f));
                }

                float background = 0.1f * C.baseEvaporation;
                float br = std::min(background, humidity*0.1f);
                humidity -= br;
                Paccum.at(x,y) += weight * br;

                prevh = h;
            }
        }
    }
}

static HeightField computePrecipMultiWind(const HeightField& H, const ClimateParams& C)
{
    HeightField P(H.w, H.h);
    auto coastDist = distanceToCoast(H, seaLevelClimate_of(C));

    // Decompose wind into cardinal passes with nonnegative weights
    float wE = std::max(0.f,  C.windX);
    float wW = std::max(0.f, -C.windX);
    float wS = std::max(0.f,  C.windY);
    float wN = std::max(0.f, -C.windY);
    float wsum = wE+wW+wS+wN;
    if (wsum <= 1e-6f) { wE = 1.0f; wsum = 1.0f; }
    wE/=wsum; wW/=wsum; wS/=wsum; wN/=wsum;

    const int passCount = std::max(1, static_cast<int>(C.passes));
    for (int pass=0; pass<passCount; ++pass) {
        // East wind: x-major left->right
        addPrecipOnePass(H, coastDist, C, 0, /*sx*/0, +1, /*sy*/0, +1, wE, P);
        // West wind: x-major right->left
        addPrecipOnePass(H, coastDist, C, 0, /*sx*/H.w-1, -1, /*sy*/0, +1, wW, P);
        // South wind: y-major top->bottom (remember +y is south)
        addPrecipOnePass(H, coastDist, C, 1, /*sx*/0, +1, /*sy*/0, +1, wS, P);
        // North wind: y-major bottom->top
        addPrecipOnePass(H, coastDist, C, 1, /*sx*/0, +1, /*sy*/H.h-1, -1, wN, P);
    }

    return P;
}

// ---------------------------------------------------------------
// 4) Priority-Flood depression filling (Barnes et al., 2014/2015).
//    Min-heap seeded by boundary (clamped to sea level).
//    Ensures every land cell drains to ocean/lake spillway.
// References: Barnes et al., arXiv & journal versions. :contentReference[oaicite:6]{index=6}
// ---------------------------------------------------------------
struct PFCell { int x,y; float z; };
struct PFCompare { bool operator()(const PFCell& a, const PFCell& b) const { return a.z > b.z; } };

static HeightField priorityFloodFill(const HeightField& H, float seaLevel)
{
    const int W = H.w, HH = H.h;
    HeightField F(W, HH);
    std::vector<uint8_t> visited(size_t(W)*HH, 0);
    std::priority_queue<PFCell, std::vector<PFCell>, PFCompare> pq;

    auto push = [&](int x,int y,float z){
        int i = idx(x,y,W);
        if (!visited[i]) {
            visited[i] = 1;
            F.at(x,y) = z;
            pq.push(PFCell{x,y,z});
        }
    };

    // Seed boundary, clamped at least to sea level
    for (int x=0;x<W;++x){ push(x,0,    std::max(H.at(x,0),    seaLevel)); }
    for (int x=0;x<W;++x){ push(x,HH-1, std::max(H.at(x,HH-1), seaLevel)); }
    for (int y=1;y<HH-1;++y){ push(0,y,    std::max(H.at(0,y),    seaLevel)); }
    for (int y=1;y<HH-1;++y){ push(W-1,y,  std::max(H.at(W-1,y),  seaLevel)); }

    while(!pq.empty()){
        PFCell c = pq.top(); pq.pop();
        for (int k=0;k<8;++k){
            int nx = c.x + DX8[k], ny = c.y + DY8[k];
            if (!inBounds(nx,ny,W,HH)) continue;
            int j = idx(nx,ny,W);
            if (visited[j]) continue;

            float raw = H.at(nx,ny);
            float zfilled = std::max(raw, c.z); // fill or keep if higher
            push(nx,ny,zfilled);
        }
    }
    return F;
}

// ---------------------------------------------------------------
// 5) Flow routing
//    A) D8 flow direction for diagnostics/export (0..7, 255=sink)
//    B) D∞ recipients (angle-proportioned split to two neighbors)
//       per Tarboton (1997): best triangular facet, steepest descent
//       vector must lie within the wedge between facet edges.
//       If D∞ fails (flat), fall back to D8.
//
// References: Tarboton D∞ algorithm (TAUDEM/QGIS docs). :contentReference[oaicite:7]{index=7}
// ---------------------------------------------------------------
#if HYDRO_EXPOSE_D8_HELPERS
[[maybe_unused]] static void computeFlowDirD8(const HeightField& F, std::vector<uint8_t>& dir, float seaLevel)
{
    const int W=F.w, H=F.h;
    dir.assign(size_t(W)*H, 255);
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            float z = F.at(x,y);
            if (z <= seaLevel) { dir[idx(x,y,W)] = 255; continue; }
            int bestK = -1; float bestSlope = 0.0f;
            for (int k=0;k<8;++k){
                int nx=x+DX8[k], ny=y+DY8[k];
                if (!inBounds(nx,ny,W,H)) continue;
                float drop = z - F.at(nx,ny);
                float s = drop / DIST8[k];
                if (s > bestSlope) { bestSlope = s; bestK = k; }
            }
            dir[idx(x,y,W)] = (bestK<0) ? 255 : uint8_t(bestK);
        }
    }
}
#endif

struct FlowRecipients {
    // up to two recipients per cell (Tarboton D∞); if D8, B is -1
    std::vector<int>   toA, toB;   // linear indices or -1
    std::vector<float> wA,  wB;    // weights, sum to 1 when both valid
    // Also store a diagnostic primary D8-like direction (0..7, 255)
    std::vector<uint8_t> primaryDir;
};

static FlowRecipients computeFlowRecipientsDInf(const HeightField& F, float seaLevel)
{
    const int W=F.w, H=F.h, N=W*H;
    FlowRecipients G;
    G.toA.assign(N, -1); G.toB.assign(N, -1);
    G.wA.assign(N, 0.0f); G.wB.assign(N, 0.0f);
    G.primaryDir.assign(N, 255);

    auto solveFacetGradient = [&](float z0,
                                  float dx1,float dy1,float z1,
                                  float dx2,float dy2,float z2,
                                  float& a,float& b)->bool
    {
        // Solve 2x2: [dx1 dy1; dx2 dy2] * [a;b] = [z1-z0; z2-z0]
        float rhs1 = z1 - z0, rhs2 = z2 - z0;
        float det = dx1*dy2 - dx2*dy1;
        if (std::fabs(det) < 1e-8f) return false;
        a = ( rhs1*dy2 - rhs2*dy1) / det; // ∂z/∂x
        b = ( dx1*rhs2 - dx2*rhs1) / det; // ∂z/∂y
        return std::isfinite(a) && std::isfinite(b);
    };

    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            int i = idx(x,y,W);
            float z0 = F.at(x,y);
            if (z0 <= seaLevel) { G.primaryDir[i]=255; continue; }

            // D8 fallback candidate for primary
            int d8best = -1; float d8slope = 0.0f;
            for (int k=0;k<8;++k){
                int nx=x+DX8[k], ny=y+DY8[k];
                if (!inBounds(nx,ny,W,H)) continue;
                float s = (z0 - F.at(nx,ny)) / DIST8[k];
                if (s > d8slope) { d8slope = s; d8best = k; }
            }
            G.primaryDir[i] = (d8best<0)?255:uint8_t(d8best);

#if HYDRO_USE_DINF
            // Evaluate 8 triangular facets; keep the one whose downslope direction lies within the wedge
            float bestSlope = 0.0f;
            int   bestK = -1;
            float bestDx = 0.0f, bestDy = 0.0f; // descent vector

            for (int k=0;k<8;++k){
                int k2 = (k+1)&7;
                int ax = x+DX8[k],  ay=y+DY8[k];
                int bx = x+DX8[k2], by=y+DY8[k2];
                if (!inBounds(ax,ay,W,H) || !inBounds(bx,by,W,H)) continue;

                float dx1 = float(DX8[k]),  dy1 = float(DY8[k]);
                float dx2 = float(DX8[k2]), dy2 = float(DY8[k2]);
                float z1 = F.at(ax,ay), z2 = F.at(bx,by);

                float a,b;
                if (!solveFacetGradient(z0, dx1,dy1,z1, dx2,dy2,z2, a,b)) continue;

                // Steepest descent direction is -grad = (-a,-b)
                float vx = -a, vy = -b;
                float gnorm = std::sqrt(vx*vx + vy*vy);
                if (!(gnorm > 0.0f)) continue;

                // Check if (-grad) lies within wedge [v1->v2] (CCW order)
                float c1 = cross2(dx1,dy1, vx,vy);
                float c2 = cross2(vx,vy,   dx2,dy2);
                float c12 = cross2(dx1,dy1, dx2,dy2); // positive for CCW wedge
                bool inside = (c12 > 0.0f) ? (c1 >= 0.0f && c2 >= 0.0f) : (c1 <= 0.0f && c2 <= 0.0f);
                if (!inside) continue;

                float slope = gnorm; // drop per unit distance along -grad
                if (slope > bestSlope) {
                    bestSlope = slope;
                    bestK = k;
                    bestDx = vx; bestDy = vy;
                }
            }

            if (bestK >= 0 && bestSlope > 0.0f) {
                // Split flow between neighbors k and k2 using barycentric proportions in the wedge
                int k2 = (bestK+1)&7;
                int ax = x+DX8[bestK],  ay=y+DY8[bestK];
                int bx = x+DX8[k2],     by=y+DY8[k2];
                if (inBounds(ax,ay,W,H) && inBounds(bx,by,W,H)) {
                    float v1x=float(DX8[bestK]),  v1y=float(DY8[bestK]);
                    float v2x=float(DX8[k2]),     v2y=float(DY8[k2]);
                    float det = cross2(v1x,v1y, v2x,v2y);
                    float lam1 = ( bestDx*v2y - bestDy*v2x) / det;
                    float lam2 = (-bestDx*v1y + bestDy*v1x) / det;
                    float sum = lam1 + lam2;

                    if (sum > 1e-8f && lam1 >= 0.0f && lam2 >= 0.0f) {
                        float w1 = lam1 / sum;
                        float w2 = lam2 / sum;

                        // Require downslope neighbors (strictly lower) for valid recipients
                        bool ok1 = F.at(ax,ay) < z0;
                        bool ok2 = F.at(bx,by) < z0;

                        if (ok1 && ok2) {
                            G.toA[i] = idx(ax,ay,W); G.wA[i] = w1;
                            G.toB[i] = idx(bx,by,W); G.wB[i] = w2;
                            continue;
                        } else if (ok1) {
                            G.toA[i] = idx(ax,ay,W); G.wA[i] = 1.0f;
                            G.toB[i] = -1;           G.wB[i] = 0.0f;
                            continue;
                        } else if (ok2) {
                            G.toA[i] = idx(bx,by,W); G.wA[i] = 1.0f;
                            G.toB[i] = -1;           G.wB[i] = 0.0f;
                            continue;
                        }
                    }
                }
            }
            // If we reach here, fall back to D8 for this cell
#endif
            if (G.primaryDir[i] != 255) {
                int k = G.primaryDir[i];
                int nx = x+DX8[k], ny = y+DY8[k];
                if (inBounds(nx,ny,W,H) && F.at(nx,ny) < z0) {
                    G.toA[i] = idx(nx,ny,W); G.wA[i] = 1.0f;
                } else {
                    G.toA[i] = -1; G.wA[i] = 0.0f;
                }
                G.toB[i] = -1; G.wB[i] = 0.0f;
            } else {
                G.toA[i] = -1; G.toB[i] = -1; G.wA[i]=G.wB[i]=0.0f;
            }
        }
    }

    return G;
}

// ---------------------------------------------------------------
// 6) Flow accumulation (works for both D8 and D∞ recipients)
//    Q[i] = supply[i] + sum_upstream (Q[u] * w(u->i))
// Topological processing (Kahn) over the flow graph.
// ---------------------------------------------------------------
static void flowAccumulation(const HeightField& F,
                             const HeightField& precip,
                             const FlowRecipients& G,
                             std::vector<float>& accum)
{
    const int W=F.w, H=F.h, N=W*H;
    accum.assign(N, 0.0f);

    // Build indegree from recipients
    std::vector<int> indeg(N, 0);
    for (int i=0;i<N;++i){
        int a = G.toA[i], b = G.toB[i];
        if (a >= 0) indeg[a]++;
        if (b >= 0) indeg[b]++;
    }

    std::queue<int> q;
    for (int i=0;i<N;++i) if (indeg[i]==0) q.push(i);

    while (!q.empty()){
        int i = q.front(); q.pop();
        int x = i % W, y = i / W;

        float supply = precip.at(x,y) + HYDRO_EPS_SUPPLY;
        float Qi = accum[i] + supply;
        accum[i] = Qi;

        int a = G.toA[i], b = G.toB[i];
        if (a >= 0) {
            accum[a] += Qi * G.wA[i];
            if (--indeg[a] == 0) q.push(a);
        }
        if (b >= 0) {
            accum[b] += Qi * G.wB[i];
            if (--indeg[b] == 0) q.push(b);
        }
    }
}

// ---------------------------------------------------------------
// 7) Strahler stream order (on a D8 backbone):
//    - compute only over cells flagged as rivers
//    - downstream link = primary D8 neighbor
//    Order rules: leaves=1; if two upstream have same order m -> parent m+1 else max.
// References: ArcGIS docs & general hydrology texts. :contentReference[oaicite:8]{index=8}
// ---------------------------------------------------------------
static std::vector<uint8_t> strahlerOrderD8(const std::vector<uint8_t>& riverMask,
                                            const std::vector<uint8_t>& d8,
                                            int W,int H)
{
    const int N=W*H;
    std::vector<uint8_t> order(N, 0);
    std::vector<int> indeg(N, 0), down(N, -1);

    // Build river-only graph
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            int i = idx(x,y,W);
            if (!riverMask[i]) continue;
            uint8_t k = d8[i];
            if (k == 255) continue;
            int nx=x+DX8[k], ny=y+DY8[k];
            if (!inBounds(nx,ny,W,H)) continue;
            int j = idx(nx,ny,W);
            if (!riverMask[j]) continue;
            down[i]=j;
            indeg[j]++;
        }
    }

    std::queue<int> q;
    for (int i=0;i<N;++i) if (riverMask[i] && indeg[i]==0) q.push(i);

    std::vector<uint8_t> maxOrd(N, 0), maxCnt(N, 0);

    while(!q.empty()){
        int i = q.front(); q.pop();
        if (order[i] == 0) order[i] = 1; // leaves are order 1
        int j = down[i];
        if (j >= 0) {
            // track upstream orders at j
            if (order[i] > maxOrd[j]) { maxOrd[j] = order[i]; maxCnt[j] = 1; }
            else if (order[i] == maxOrd[j]) { maxCnt[j] = uint8_t(maxCnt[j]+1); }

            if (--indeg[j] == 0) {
                order[j] = (maxCnt[j] >= 2) ? uint8_t(maxOrd[j]+1) : maxOrd[j];
                q.push(j);
            }
        }
    }
    return order;
}

// ---------------------------------------------------------------
// 8) Gaussian blur (separable) used to softly widen channels
// ---------------------------------------------------------------
static void gaussianBlur1D(float* data, int W, int H, float sigma)
{
    if (sigma <= 0.0f) return;
    int r = std::max(1, int(std::ceil(3.0f * sigma)));
    std::vector<float> k(2*r+1);
    float sum=0.0f, s2=2.0f*sigma*sigma;
    for (int i=-r;i<=r;++i){ float w=std::exp(-(i*i)/s2); k[i+r]=w; sum+=w; }
    for (float& v: k) v/=sum;

    std::vector<float> tmp(size_t(W)*H, 0.0f);

    // Horizontal
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            float s=0.0f;
            for (int i=-r;i<=r;++i){
                int xx = clamp(x+i, 0, W-1);
                s += data[idx(xx,y,W)] * k[i+r];
            }
            tmp[idx(x,y,W)] = s;
        }
    }
    // Vertical
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            float s=0.0f;
            for (int i=-r;i<=r;++i){
                int yy = clamp(y+i, 0, H-1);
                s += tmp[idx(x,yy,W)] * k[i+r];
            }
            data[idx(x,y,W)] = s;
        }
    }
}

// ---------------------------------------------------------------
// 9) Channel carving (stream power) + lakes + water surface
//    - River mask from discharge threshold
//    - Order-aware incision (wider/deeper for higher order)
//    - Light smoothing to reduce terracing
//    References: Stream power law & parameterization reviews. :contentReference[oaicite:9]{index=9}
// ---------------------------------------------------------------
static float localSteepestSlope(const HeightField& F, int x,int y)
{
    float z = F.at(x,y), best=0.0f;
    for (int k=0;k<8;++k){
        int nx=x+DX8[k], ny=y+DY8[k];
        if (!inBounds(nx,ny,F.w,F.h)) continue;
        float s = (z - F.at(nx,ny)) / DIST8[k];
        if (s > best) best = s;
    }
    return std::max(best, HYDRO_MIN_SLOPE);
}

static HeightField carveChannels(const HeightField& base,
                                 const HeightField& filled,
                                 const std::vector<uint8_t>& d8Primary,
                                 const std::vector<float>& accum,
                                 const HydroParams& Hpar,
                                 HeightField& outWaterLevel,
                                 std::vector<uint8_t>& outRiverMask,
                                 std::vector<uint8_t>& outLakeMask)
{
    const int W=base.w, H=base.h, N=W*H;
    const float sea = seaLevel_of(Hpar);

    // Lakes where fill-depth exceeds threshold
    outLakeMask.assign(N, 0);
    for (int i=0;i<N;++i){
        float depth = filled.data[i] - base.data[i];
        if (depth >= Hpar.lakeMinDepth) outLakeMask[i] = 1;
    }

    // Preliminary river mask by discharge
    outRiverMask.assign(N, 0);
    for (int i=0;i<N;++i){
        if (accum[i] >= Hpar.riverThreshold) outRiverMask[i] = 1;
    }

    // Order-aware widening: compute Strahler order on D8 backbone restricted to river cells
    std::vector<uint8_t> order = strahlerOrderD8(outRiverMask, d8Primary, W, H);

    // Base incision field from stream power E = K A^m S^n
    std::vector<float> incision(N, 0.0f);
    const float mExp = incision_m_of(Hpar);
    const float nExp = incision_n_of(Hpar);

    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            int i = idx(x,y,W);
            if (!outRiverMask[i]) continue;
            if (filled.at(x,y) <= sea) continue; // ocean excluded
            if (outLakeMask[i]) continue;        // lake surfaces excluded

            float A = accum[i];
            float S = localSteepestSlope(filled, x,y);
            float E = Hpar.incisionK * std::pow(std::max(0.0f,A), mExp)
                                   * std::pow(std::max(HYDRO_MIN_SLOPE,S), nExp);

            // Scale by order to widen/deepen higher-order channels
            float ord = std::max<uint8_t>(1, order[i]);
            float widen = 0.75f + 0.5f * (ord - 1.0f); // 1→0.75, 2→1.25, 3→1.75, ...
            incision[i] = E * widen;
        }
    }

    // Soften with Gaussian blur (bankWidth interpreted as sigma in cells)
    HeightField carved = base;
    if (Hpar.bankWidth > 0.1f) {
        gaussianBlur1D(incision.data(), W, H, Hpar.bankWidth * 0.5f);
    }

    // Apply incision, clamp to sea level
    for (int i=0;i<N;++i){
        carved.data[i] = std::max(sea, carved.data[i] - incision[i]);
    }

    // Light smoothing along river cells to remove small terracing artifacts
    const int smoothIters = smoothIterations_of(Hpar);
    for (int it=0; it<smoothIters; ++it){
        std::vector<float> tmp = carved.data;
        for (int y=0;y<H;++y){
            for (int x=0;x<W;++x){
                int i = idx(x,y,W);
                if (!outRiverMask[i]) continue;
                float c = carved.at(x,y);
                float s=0.0f; int cnt=0;
                if (x>0)   { s+=carved.at(x-1,y); ++cnt; }
                if (x<W-1) { s+=carved.at(x+1,y); ++cnt; }
                if (y>0)   { s+=carved.at(x,y-1); ++cnt; }
                if (y<H-1) { s+=carved.at(x,y+1); ++cnt; }
                tmp[i] = 0.5f*c + 0.5f*(s/std::max(1,cnt));
            }
        }
        carved.data.swap(tmp);
    }

    // Water surface assembly
    outWaterLevel = HeightField(W,H);
    for (int y=0;y<H;++y){
        for (int x=0;x<W;++x){
            int i = idx(x,y,W);
            if (filled.at(x,y) <= sea) {
                outWaterLevel.at(x,y) = sea;            // ocean
            } else if (outLakeMask[i]) {
                outWaterLevel.at(x,y) = filled.at(x,y); // lake surface at spill level
            } else if (outRiverMask[i]) {
                outWaterLevel.at(x,y) = carved.at(x,y) + 0.12f; // shallow skim over channel
            } else {
                outWaterLevel.at(x,y) = -1e6f; // "no water"
            }
        }
    }

    return carved;
}

// ---------------------------------------------------------------
// 10) Public entry point
// ---------------------------------------------------------------
HydroOutputs buildHydrology(const HeightField& baseHeight,
                            const ClimateParams& climate,
                            const HydroParams& hydro)
{
    [[maybe_unused]] const int W = baseHeight.w;
    [[maybe_unused]] const int H = baseHeight.h;
    const float sea = seaLevel_of(hydro);

    // 1) Climate fields
    HeightField temperature = computeTemperature(baseHeight, climate);
    HeightField precip      = computePrecipMultiWind(baseHeight, climate);

    // 2) Fill depressions to guarantee drainage (to sea/lake spillways)
    HeightField filled = priorityFloodFill(baseHeight, sea);

    // 3) Flow routing (Tarboton D∞ recipients + D8 diagnostic)
    FlowRecipients G = computeFlowRecipientsDInf(filled, sea);
    std::vector<uint8_t> d8Primary = G.primaryDir; // for diagnostics, orders, and mask topology

    // 4) Flow accumulation (rain-weighted)
    std::vector<float> flowAccum;
    flowAccumulation(filled, precip, G, flowAccum);

    // 5) Carve channels, assemble water surface, lake/river masks
    std::vector<uint8_t> riverMask, lakeMask;
    HeightField waterLevel;
    HeightField carved = carveChannels(baseHeight, filled, d8Primary, flowAccum,
                                       hydro, waterLevel, riverMask, lakeMask);

    // 6) Pack outputs
    HydroOutputs out;
    out.precip      = std::move(precip);
    out.temperature = std::move(temperature);
    out.filled      = std::move(filled);
    out.carved      = std::move(carved);
    out.waterLevel  = std::move(waterLevel);
    out.flowDir     = std::move(d8Primary);
    out.flowAccum   = std::move(flowAccum);
    out.riverMask   = std::move(riverMask);
    out.lakeMask    = std::move(lakeMask);
    return out;
}

} // namespace cg
