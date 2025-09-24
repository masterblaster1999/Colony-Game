// src/proc/TerrainGen.cpp  (Windows-only stub; replaces HLSL types with C++)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")

using namespace DirectX;

// Pseudocode-style driver for CS_GenHeight + CS_Erode (C++-compilable stub)
struct HeightGenParams {
    XMFLOAT2 worldScale{1.0f, 1.0f};
    XMFLOAT2 offset{0.0f, 0.0f};
    int   oct   = 5;
    float lac   = 2.0f;
    float gain  = 0.5f;
    float warp  = 2.0f;
};

struct ErodeParams {
    float erodeK   = 0.12f;
    float depositK = 0.15f;
};

void GenerateTerrain(ID3D11Device* dev,
                     ID3D11DeviceContext* ctx,
                     ID3D11ComputeShader* csGen,
                     ID3D11ComputeShader* csErode,
                     ID3D11ShaderResourceView** outHeightSRV,
                     int W, int H,
                     const HeightGenParams& gp,
                     const ErodeParams& ep,
                     int steps)
{
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(csGen);
    UNREFERENCED_PARAMETER(csErode);
    UNREFERENCED_PARAMETER(W);
    UNREFERENCED_PARAMETER(H);
    UNREFERENCED_PARAMETER(gp);
    UNREFERENCED_PARAMETER(ep);
    UNREFERENCED_PARAMETER(steps);

    // TODO: Implement compute-based terrain generation:
    //  - Create UAV height A/B
    //  - Bind CS_GenHeight -> UAV A
    //  - Dispatch ceil(W/8), ceil(H/8)
    //  - Loop erode steps: bind A->SRV, B->UAV with CS_Erode; swap A/B
    //  - Return SRV of latest to *outHeightSRV
    if (outHeightSRV) {
        *outHeightSRV = nullptr; // stub result to keep callers safe
    }
}
