// -----------------------------------------------------------------------------
// shaders/water_tiny.hlsl
// Tiny water shader for seas/lakes (Gerstner waves) and rivers (flow-aligned
// ripples if a flow map is bound).
//
// References:
//  - GPU Gems Ch.1, "Effective Water Simulation from Physical Models" (gerstner & normals)
//  - Schlick Fresnel F = F0 + (1-F0)*(1-cosTheta)^5
// -----------------------------------------------------------------------------

cbuffer WaterMatrices : register(b0) {
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float3   gCameraPosW;
    float    gTime;
};

#define MAX_WAVES 4

cbuffer WaterParams : register(b1) {
    // Wave set
    int     gWaveCount;
    float   gGravity;                 // not used unless you want ω = sqrt(g*k); kept for future
    float2  _pad0;

    float2  gWaveDir   [MAX_WAVES];   // unit XZ directions
    float   gWaveAmp   [MAX_WAVES];   // amplitude (world units)
    float   gWaveLen   [MAX_WAVES];   // wavelength (world units)
    float   gWaveSpeed [MAX_WAVES];   // phase speed (world units/sec)
    float   gWaveSteep [MAX_WAVES];   // steepness Q (0..1), choppiness

    // Shading
    float3  gDeepColor;               // deep water albedo
    float   gSpecularPower;           // spec exponent
    float3  gShallowColor;            // shallow tint
    float   gF0;                      // Fresnel F0 scalar (water ~0.02)
    float3  gSunDirW;                 // normalized
    float   gRiverRippleAmp;          // amplitude of river ripples (if flow map present)
    float3  gSunColor;                // direct light color
    float   gRiverRippleFreq;         // ripples per world unit along flow
    float2  gFlowUVScale;             // flow map UV scale for world XZ
    float2  gFlowUVOffset;            // flow map UV offset
    float   gRiverRippleSpeed;        // animation speed for river ripples
    int     gUseFlowMap;              // 0=no, 1=yes
};

Texture2D    gFlowMap   : register(t0); // RG: flow dir in [-1,1], B: speed [0,1], A: unused
SamplerState gSamplerLin : register(s0);

struct VSIn {
    float3 pos : POSITION;   // water mesh in local space (a grid)
    float3 nrm : NORMAL;     // usually (0,1,0)
    float2 uv  : TEXCOORD0;  // optional
};

struct VSOut {
    float4 posH     : SV_POSITION;
    float3 posW     : TEXCOORD0;
    float3 nrmW     : TEXCOORD1;
    float2 flowUV   : TEXCOORD2;
};

static float PI = 3.14159265f;

// Evaluate Gerstner displacement + derivatives at a local XZ point (world space)
void GerstnerWaves(float2 xz, out float3 disp, out float3 dPdX, out float3 dPdZ)
{
    disp = float3(0,0,0);
    // Tangents of displaced surface used for normal: ∂P/∂x and ∂P/∂z
    // Initialize to identity (∂x, ∂y, ∂z) basis
    dPdX = float3(1, 0, 0);
    dPdZ = float3(0, 0, 1);

    [loop]
    for (int i=0; i<gWaveCount && i<MAX_WAVES; ++i)
    {
        float2  D = normalize(gWaveDir[i]);
        float   A = gWaveAmp[i];
        float   L = max(1e-4, gWaveLen[i]);
        float   k = 2.0f * PI / L;          // wave number
        float   w = gWaveSpeed[i] * k;      // simple dispersion
        float   Q = gWaveSteep[i];          // steepness (0..1)

        float   phase = k * dot(D, xz) - w * gTime;
        float   s = sin(phase);
        float   c = cos(phase);

        // Displacement (horizontal choppiness + vertical)
        disp.xz += (Q * A) * D * c;
        disp.y  += A * s;

        // Derivatives for normal from cross(∂P/∂z, ∂P/∂x)
        // x' = x + Q*A*D.x*cos(phase)
        // z' = z + Q*A*D.y*cos(phase)
        // y' =    A*sin(phase)
        float kQADx = k * Q * A * D.x;
        float kQADz = k * Q * A * D.y;
        float kADx  = k * A * D.x;
        float kADz  = k * A * D.y;

        // ∂/∂x
        dPdX.x += -kQADx * s * D.x;  // 1 - Q*A*k*(D.x)^2*sin
        dPdX.z += -kQADx * s * D.y;  //      - Q*A*k*(D.x*D.y)*sin
        dPdX.y +=  kADx   * c;       //        A*k*D.x*cos

        // ∂/∂z
        dPdZ.x += -kQADz * s * D.x;
        dPdZ.z += -kQADz * s * D.y;  // 1 - Q*A*k*(D.y)^2*sin
        dPdZ.y +=  kADz   * c;
    }
}

VSOut VSWater(VSIn IN)
{
    VSOut OUT;

    // Local to world
    float4 posW4 = mul(float4(IN.pos,1.0), gWorld);
    float3 posW  = posW4.xyz;

    // Base plane in world XZ; evaluate waves at original projected XZ
    float2 xz = posW.xz;
    float3 disp, dPdX, dPdZ;
    GerstnerWaves(xz, disp, dPdX, dPdZ);

    // Apply displacement in world space (assumes gWorld is uniform scale)
    posW += disp;

    // World-space normal from displaced tangents
    float3 N = normalize(cross(dPdZ, dPdX));
    float4 v4 = mul(float4(posW,1), gView);
    OUT.posH = mul(v4, gProj);
    OUT.posW = posW;
    OUT.nrmW = N;

    // Flow-map UV (for rivers); map world XZ -> UV
    OUT.flowUV = xz * gFlowUVScale + gFlowUVOffset;
    return OUT;
}

float3 fresnelSchlick(float cosTheta, float F0)
{
    // Schlick's approximation; water F0 ~ 0.02–0.04. :contentReference[oaicite:4]{index=4}
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Optional flow-aligned ripple normal for rivers (tiny, stylistic)
float3 RiverRippleNormal(float2 uv, float3 baseN, float3 posW)
{
    if (gUseFlowMap == 0) return baseN;

    float4 flowSample = gFlowMap.Sample(gSamplerLin, uv);
    float2 flowDir = normalize(flowSample.xy * 2.0 - 1.0);   // [-1,1]
    float  speed   = flowSample.z;                           // [0,1] proxy

    // Create a sine ripple along the flow direction in XZ and turn its gradient into a tilt
    float  theta = gRiverRippleFreq * dot(flowDir, posW.xz) - gRiverRippleSpeed * gTime * (0.25 + 0.75*speed);
    float  s = sin(theta);
    float  a = gRiverRippleAmp * (0.25 + 0.75*speed);        // speed intensifies ripples

    // Approximate normal from gradient: d(height)/dx ~ a*cos(theta)*flowDir.x, same for y
    float3 rippleN = normalize(float3(-a * cos(theta) * flowDir.x,
                                      1.0,
                                      -a * cos(theta) * flowDir.y));
    // Mix with base normal; keep the result normalized
    float3 N = normalize(lerp(baseN, rippleN, 0.35));
    return N;
}

struct PSOut {
    float4 color : SV_TARGET0;
};

PSOut PSWater(VSOut IN)
{
    PSOut O;
    float3 V = normalize(gCameraPosW - IN.posW);
    float3 L = normalize(gSunDirW);
    float3 N = normalize(IN.nrmW);

    // River ripples if flow map is present (maintains tiny footprint)
    N = RiverRippleNormal(IN.flowUV, N, IN.posW);

    float3 H = normalize(L + V);
    float  ndotl = saturate(dot(N, L));
    float  ndotv = saturate(dot(N, V));
    float  ndoth = saturate(dot(N, H));

    // Base color: mix deep & shallow by N.y as a cheap proxy (more upright -> shallower)
    float shallow = saturate(0.5 + 0.5 * N.y);
    float3 baseCol = lerp(gDeepColor, gShallowColor, shallow);

    // Specular from sun + simple Fresnel
    float3 F = fresnelSchlick(ndotv, gF0);
    float  spec = pow(ndoth, gSpecularPower) * ndotl;
    float3 specCol = F * spec * gSunColor;

    // Simple lambert-ish term, dimmed (water is mainly reflective)
    float3 diffuse = baseCol * (0.15 + 0.85*ndotl) * (1.0 - F);

    O.color.rgb = diffuse + specCol;
    O.color.a   = 1.0;
    return O;
}
