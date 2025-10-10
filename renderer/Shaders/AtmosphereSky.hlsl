// renderer/Shaders/AtmosphereSky.hlsl
// Starter sky: single-scatter raymarch with Bruneton-style parameters.
// Drop-in full-screen pass; later swap to precomputed LUTs.
// References: Bruneton & Neyret 2008 (CGF/Wiley); Unreal/Frostbite implementations. 

cbuffer SkyCB : register(b0)
{
    float4x4 InvViewProj;         // inverse view-projection
    float3   CameraWS;  float TimeSeconds;
    float2   ScreenSize; float TimeOfDay01; // 0..1 day cycle
    float    Humidity01; float3 SunColor;   // Sun radiance scale
    float3   MoonColor;  float  EnableMoon; // 0/1 toggle
};

// Optional future use (Bruneton LUTs)
Texture2D   TransmittanceLUT : register(t0);
Texture3D   ScatteringLUT    : register(t1);
SamplerState LinearClamp     : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    // Fullscreen triangle
    float2 uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    VSOut o;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = uv;
    return o;
}

// ----------------- Atmosphere parameters (Earth-like) -----------------
static const float kPlanetRadius      = 6360000.0; // meters
static const float kAtmosphereTop     = 6460000.0; // meters (10 km)
static const float kHr = 8000.0;   // Rayleigh scale height
static const float kHm = 1200.0;   // Mie scale height

// Wavelength-dependent Rayleigh scattering (RGB in m^-1)
static const float3 kBetaRayleigh = float3(5.802e-6, 13.558e-6, 33.1e-6);
// Mie (approx; will be scaled by humidity)
static const float3 kBetaMieSca   = float3(3.996e-6, 3.996e-6, 3.996e-6);
static const float3 kBetaMieExt   = kBetaMieSca * 1.1; // ext > sca

// Phase functions
float RayleighPhase(float mu) { return 3.0/(16.0*3.14159) * (1.0 + mu*mu); }
float HenyeyGreenstein(float mu, float g)
{
    float g2 = g*g;
    return (1.0/(4.0*3.14159)) * ((1.0 - g2) / pow(abs(1.0 + g2 - 2.0*g*mu), 1.5));
}

// Utility
float2 RaySphere(float3 ro, float3 rd, float radius)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius*radius;
    float h = b*b - c;
    if (h < 0.0) return float2(1e20, 1e20);
    h = sqrt(h);
    return float2(-b - h, -b + h);
}

// Build view ray from screen uv
float3 GetViewRay(float2 uv)
{
    float4 ndc = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    float4 ws  = mul(InvViewProj, ndc);
    ws /= ws.w;
    return normalize(ws.xyz - CameraWS);
}

// Sun & moon direction from time-of-day (simple model)
void GetSunMoonDirs(out float3 sunDir, out float3 moonDir)
{
    // Sun circles around X axis across the sky
    float ang = (TimeOfDay01 * 2.0 * 3.14159);   // 0..2pi
    float  el = sin(ang) * 0.85;                 // elevation
    float  az = cos(ang) * 0.0;                  // fixed azimuth for simplicity
    float s  = cos(el);
    sunDir = normalize(float3(0.0, s, -sin(el))); // points from world towards sun
    moonDir = -sunDir;
}

// Density along altitude
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
    if (t0 > t1) return 0;

    // March
    const int   STEPS = 16;
    float       dt    = (t1 - t0) / STEPS;

    float3 betaR = kBetaRayleigh;
    float3 betaM = lerp(kBetaMieSca*0.5, kBetaMieSca*3.0, saturate(Humidity01));
    float3 betaE = betaR + kBetaMieExt * (0.8 + 0.4*Humidity01);

    float3 L = 0;
    float  t  = t0;
    float3 tau = 0; // optical depth camera->sample

    for (int i = 0; i < STEPS; ++i)
    {
        float3 p = ro + rd * (t + 0.5*dt);
        float3 pc = normalize(p) * kPlanetRadius;   // closest ground point (for alt)
        float height = length(p);

        float rhoR, rhoM;
        DensityAtHeight(height, rhoR, rhoM);

        float3 dTau = (betaR*rhoR + betaM*rhoM) * dt;
        tau += dTau;

        // Attenuation camera->sample
        float3 Tview = exp(-tau);

        // Light optical depth (single sample toward sun)
        float2 tls = RaySphere(p, lightDir, kAtmosphereTop);
        float  ls  = max(0.0, tls.y);
        float3 tauL = (betaR*rhoR + betaM*rhoM) * ls * 0.5; // cheap approx
        float3 Tlight = exp(-tauL);

        float mu = dot(rd, lightDir);
        float phase = RayleighPhase(mu);
        float mie   = HenyeyGreenstein(mu, 0.8);

        float3 S = (betaR * phase * rhoR + betaM * mie * rhoM) * Tlight;

        L += Tview * S * dt;

        t += dt;
    }

    return L;
}

float3 ToneMapACES(float3 x)
{
    // Simple ACES approximation
    const float a = 2.51; const float b = 0.03;
    const float c = 2.43; const float d = 0.59; const float e = 0.14;
    return saturate((x*(a*x + b)) / (x*(c*x + d) + e));
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 rd = GetViewRay(i.uv);
    float3 ro = CameraWS;

    float3 sunDir, moonDir;
    GetSunMoonDirs(sunDir, moonDir);

    float3 skySun  = IntegrateAtmosphere(ro, rd, sunDir) * SunColor;
    float3 skyMoon = EnableMoon > 0.5 ? IntegrateAtmosphere(ro, rd, moonDir) * MoonColor : 0;

    float3 col = skySun + skyMoon;

    // Simple horizon/ground fade (if no terrain in this pass)
    float horizon = saturate(rd.y * 0.5 + 0.5);
    col *= lerp(0.04, 1.0, horizon);

    return float4(ToneMapACES(col), 1.0);
}
