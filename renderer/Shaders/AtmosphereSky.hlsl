// renderer/Shaders/AtmosphereSky.hlsl
// Single-scatter sky (raymarch) — DXC/SM6 friendly.
// Notes:
//  - Provides VSMain (fullscreen triangle) and PSMain (sky), plus a pixel-shader alias 'main'
//    to satisfy build setups that still compile with /E main.
//  - Normalized UVs [0..1] are passed to the PS; PS reconstructs a view ray via InvViewProj.
//  - Unused vars removed; explicit float3(0,0,0) returns; minor numeric guards.

// -----------------------------------------------------------------------------
// Constants & resources
// -----------------------------------------------------------------------------
cbuffer SkyCB : register(b0)
{
    float4x4 InvViewProj;         // inverse view-projection (DX: depth in [0,1])
    float3   CameraWS;   float TimeSeconds;
    float2   ScreenSize; float TimeOfDay01; // 0..1 day cycle
    float    Humidity01; float3 SunColor;   // Sun radiance scale
    float3   MoonColor;  float  EnableMoon; // 0/1 toggle
};

// Optional future use (Bruneton LUTs)
Texture2D   TransmittanceLUT : register(t0);
Texture3D   ScatteringLUT    : register(t1);
SamplerState LinearClamp     : register(s0);

static const float PI         = 3.14159265359;
static const int   SKY_STEPS  = 16; // raymarch steps for atmosphere integration

// -----------------------------------------------------------------------------
// VS/PS I/O
// -----------------------------------------------------------------------------
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;   // normalized [0..1], origin at top-left
};

// Fullscreen triangle via SV_VertexID (no VB/IB needed).
// Generates positions covering the screen and UVs normalized to [0..1].
VSOut VSMain(uint id : SV_VertexID)
{
    // uv2 in { (0,0), (2,0), (0,2) }
    float2 uv2 = float2((id << 1) & 2, id & 2);

    VSOut o;
    // Clip-space position with Y flip for D3D (top-left origin in screen space)
    o.pos = float4(uv2 * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

    // Normalize to [0..1] for the pixel shader
    o.uv = uv2 * 0.5;
    return o;
}

// -----------------------------------------------------------------------------
// Atmosphere parameters (approx Earth-like)
// -----------------------------------------------------------------------------
static const float kPlanetRadius  = 6360000.0; // meters
static const float kAtmosphereTop = 6460000.0; // meters (10 km)
static const float kHr = 8000.0;   // Rayleigh scale height
static const float kHm = 1200.0;   // Mie scale height

// Wavelength-dependent Rayleigh scattering (RGB in m^-1)
static const float3 kBetaRayleigh = float3(5.802e-6, 13.558e-6, 33.1e-6);
// Mie (approx; scaled by humidity)
static const float3 kBetaMieSca   = float3(3.996e-6, 3.996e-6, 3.996e-6);
static const float3 kBetaMieExt   = kBetaMieSca * 1.1; // extinction > scatter

// -----------------------------------------------------------------------------
// Phase functions & helpers
// -----------------------------------------------------------------------------
float RayleighPhase(float mu)
{
    return 3.0 / (16.0 * PI) * (1.0 + mu * mu);
}

float HenyeyGreenstein(float mu, float g)
{
    float g2 = g * g;
    // Guard denominator slightly to avoid pow(0, -1.5) at extreme mu≈±1,g≈1
    float denom = max(1e-3, 1.0 + g2 - 2.0 * g * mu);
    return (1.0 / (4.0 * PI)) * ((1.0 - g2) / pow(denom, 1.5));
}

// Ray-sphere intersection with sphere centered at origin
float2 RaySphere(float3 ro, float3 rd, float radius)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return float2(1e20, 1e20);
    h = sqrt(h);
    return float2(-b - h, -b + h);
}

// Build a view ray from normalized screen UV ([0..1], origin at top-left).
// We map to NDC, flip Y for D3D, unproject with InvViewProj, and subtract CameraWS.
float3 GetViewRay(float2 uv01)
{
    float2 ndc = float2(uv01.x * 2.0 - 1.0, (1.0 - uv01.y) * 2.0 - 1.0);
    // Use z=1 (far plane) to get a stable direction
    float4 ws = mul(InvViewProj, float4(ndc, 1.0, 1.0));
    ws /= ws.w;
    return normalize(ws.xyz - CameraWS);
}

// Sun & moon direction from time-of-day (simple model)
void GetSunMoonDirs(out float3 sunDir, out float3 moonDir)
{
    float ang = (TimeOfDay01 * 2.0 * PI); // 0..2pi
    float el  = sin(ang) * 0.85;          // elevation curve
    float c   = cos(el);
    // Sun path rotates around X-axis
    sunDir  = normalize(float3(0.0,  c, -sin(el)));
    moonDir = -sunDir;
}

// Density at altitude
void DensityAtHeight(float height, out float rhoR, out float rhoM)
{
    float h = max(0.0, height - kPlanetRadius);
    rhoR = exp(-h / kHr);
    rhoM = exp(-h / kHm);
}

// Integrate single scattering along view ray to atmosphere top
float3 IntegrateAtmosphere(float3 ro, float3 rd, float3 lightDir)
{
    // Enter atmosphere shell
    float2 tAtm = RaySphere(ro, rd, kAtmosphereTop);
    float t0 = max(tAtm.x, 0.0);
    float t1 = tAtm.y;
    if (t0 > t1) return float3(0.0, 0.0, 0.0);

    float dt = (t1 - t0) / SKY_STEPS;

    float3 betaR = kBetaRayleigh;
    float3 betaM = lerp(kBetaMieSca * 0.5, kBetaMieSca * 3.0, saturate(Humidity01));
    // kBetaMieExt is available if you want to use a separate extinction term later.

    float3 L   = float3(0.0, 0.0, 0.0);
    float  t   = t0;
    float3 tau = float3(0.0, 0.0, 0.0); // optical depth camera->sample

    [loop]
    for (int i = 0; i < SKY_STEPS; ++i)
    {
        float3 p = ro + rd * (t + 0.5 * dt);
        float  height = length(p);

        float rhoR, rhoM;
        DensityAtHeight(height, rhoR, rhoM);

        float3 sigma = betaR * rhoR + betaM * rhoM;

        float3 dTau = sigma * dt;
        tau += dTau;

        // Attenuation camera->sample
        float3 Tview = exp(-tau);

        // Light optical depth (single sample toward sun) — cheap approx
        float2 tls  = RaySphere(p, lightDir, kAtmosphereTop);
        float  ls   = max(0.0, tls.y);
        float3 tauL = sigma * (ls * 0.5);
        float3 Tlight = exp(-tauL);

        float mu     = dot(rd, lightDir);
        float phaseR = RayleighPhase(mu);
        float phaseM = HenyeyGreenstein(mu, 0.8);

        float3 S = (betaR * phaseR * rhoR + betaM * phaseM * rhoM) * Tlight;

        L += Tview * S * dt;
        t += dt;
    }

    return L;
}

float3 ToneMapACES(float3 x)
{
    // ACESApprox (Narkowicz)
    const float a = 2.51; const float b = 0.03;
    const float c = 2.43; const float d = 0.59; const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// -----------------------------------------------------------------------------
// Pixel shader
// -----------------------------------------------------------------------------
float4 PSMain(VSOut i) : SV_Target
{
    // Build view ray from normalized UV
    float3 rd = GetViewRay(i.uv);
    float3 ro = CameraWS;

    float3 sunDir, moonDir;
    GetSunMoonDirs(sunDir, moonDir);

    float3 skySun  = IntegrateAtmosphere(ro, rd, sunDir)  * SunColor;
    float3 skyMoon = (EnableMoon > 0.5) ? IntegrateAtmosphere(ro, rd, moonDir) * MoonColor
                                        : float3(0.0, 0.0, 0.0);

    float3 col = skySun + skyMoon;

    // Simple horizon/ground fade (if no terrain in this pass)
    float horizon = saturate(rd.y * 0.5 + 0.5);
    col *= lerp(0.04, 1.0, horizon);

    return float4(ToneMapACES(col), 1.0);
}

// Optional alias to satisfy builds that still compile with /E main for PS.
// DXC will pick the 'main' matching the shader stage (/T ps_6_0).
float4 main(VSOut i) : SV_Target
{
    return PSMain(i);
}
