#include "common/common.hlsli"

// Entry: VSMain (kept to match your CMake setup)
PSIn VSMain(VSIn vin)
{
    PSIn vout;
    // Screen-space quad: pass through XY, set Z=0, W=1
    vout.Position = float4(vin.Position.xy, 0.0f, 1.0f);
    vout.Tex      = vin.Tex;
    return vout;
}
