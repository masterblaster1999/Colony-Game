#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <cmath>

#include "D3DUtils.h"
#include "PoissonDisk.h"

using Microsoft::WRL::ComPtr;

static void SaveRaw(const std::string& path, const std::vector<float>& data)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size() * sizeof(float)));
}

static void SavePGM8(const std::string& path, const std::vector<float>& data, uint32_t w, uint32_t h)
{
    // Scale [min,max] -> [0,255]
    float mn = 1e9f, mx = -1e9f;
    for (float v : data) { mn = std::min(mn, v); mx = std::max(mx, v); }
    float scale = (mx > mn) ? (255.0f / (mx - mn)) : 1.0f;

    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << w << " " << h << "\n255\n";
    for (uint32_t i = 0; i < w * h; ++i)
    {
        int iv = int(std::clamp((data[i] - mn) * scale, 0.0f, 255.0f));
        unsigned char b = static_cast<unsigned char>(iv);
        f.write(reinterpret_cast<const char*>(&b), 1);
    }
}

struct ErodeCB
{
    UINT Width;
    UINT Height;
    float Talus;
    float Strength;
    // pad to 16 bytes multiple
};

static float BilinearSample(const std::vector<float>& h, uint32_t W, uint32_t H, float x, float y)
{
    x = std::clamp(x, 0.0f, float(W - 1));
    y = std::clamp(y, 0.0f, float(H - 1));
    int x0 = int(x), y0 = int(y);
    int x1 = std::min(x0 + 1, int(W - 1));
    int y1 = std::min(y0 + 1, int(H - 1));
    float tx = x - float(x0);
    float ty = y - float(y0);
    float h00 = h[size_t(y0) * W + x0];
    float h10 = h[size_t(y0) * W + x1];
    float h01 = h[size_t(y1) * W + x0];
    float h11 = h[size_t(y1) * W + x1];
    float a = h00 * (1 - tx) + h10 * tx;
    float b = h01 * (1 - tx) + h11 * tx;
    return a * (1 - ty) + b * ty;
}

int main()
{
    try
    {
        // -----------------------------
        // D3D11 device (compute only)
        // -----------------------------
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> ctx;

        // FIX: Avoid preprocessor directives inside a function-like macro argument (MSVC C5101/C2059).
        // Build the Flags value first, then call inside HR_CHECK.
        UINT deviceFlags = 0;
    #ifdef _DEBUG
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        HR_CHECK(D3D11CreateDevice(
            nullptr,                     // Adapter
            D3D_DRIVER_TYPE_HARDWARE,    // DriverType
            0,                           // Software
            deviceFlags,                 // Flags (precomputed)
            nullptr, 0,                  // Feature levels (nullptr = default)
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            nullptr,
            ctx.GetAddressOf()));

        // Terrain size
        const UINT W = 512;
        const UINT H = 512;

        // -----------------------------
        // Resources
        // -----------------------------
        ComPtr<ID3D11Texture2D> heightA, heightB;
        ComPtr<ID3D11ShaderResourceView> heightA_SRV, heightB_SRV;
        ComPtr<ID3D11UnorderedAccessView> heightA_UAV, heightB_UAV;

        d3d::CreateFloatTextureUAVSRV(device.Get(), W, H, heightA, heightA_SRV, heightA_UAV);
        d3d::CreateFloatTextureUAVSRV(device.Get(), W, H, heightB, heightB_SRV, heightB_UAV);

        // Outflow buffer (RGBA32F): +X, -X, +Y, -Y
        ComPtr<ID3D11Texture2D> outflowTex;
        ComPtr<ID3D11ShaderResourceView> outflowSRV;
        ComPtr<ID3D11UnorderedAccessView> outflowUAV;
        d3d::CreateRGBA32FTextureUAVSRV(device.Get(), W, H, outflowTex, outflowSRV, outflowUAV);

        // -----------------------------
        // Initialize height field (simple ridges)
        // -----------------------------
        std::vector<float> initH(size_t(W) * H);
        for (UINT y = 0; y < H; ++y)
        {
            for (UINT x = 0; x < W; ++x)
            {
                float fx = float(x) / float(W);
                float fy = float(y) / float(H);
                float v = 0.5f
                        + 0.20f * sinf(10.0f * fx) * cosf(10.0f * fy)
                        + 0.15f * sinf(20.0f * fy)
                        + 0.10f * cosf(18.0f * fx + 4.0f * fy)
                        + 0.05f * (fx - 0.5f)
                        ;
                initH[size_t(y) * W + x] = std::clamp(v, 0.0f, 1.0f);
            }
        }
        d3d::UpdateFloatTexture(ctx.Get(), heightA.Get(), initH.data(), W, H);

        // -----------------------------
        // Compile compute shaders
        // -----------------------------
        const std::wstring shaderDir = L"shaders\\";
        auto csOutflowBlob = d3d::CompileShaderFromFile(shaderDir + L"ThermalOutflowCS.hlsl", L"CSMain", "cs_5_0");
        auto csApplyBlob   = d3d::CompileShaderFromFile(shaderDir + L"ThermalApplyCS.hlsl",   L"CSMain", "cs_5_0");

        ComPtr<ID3D11ComputeShader> csOutflow, csApply;
        HR_CHECK(device->CreateComputeShader(csOutflowBlob->GetBufferPointer(), csOutflowBlob->GetBufferSize(), nullptr, csOutflow.GetAddressOf()));
        HR_CHECK(device->CreateComputeShader(csApplyBlob->GetBufferPointer(),   csApplyBlob->GetBufferSize(),   nullptr, csApply.GetAddressOf()));

        // -----------------------------
        // Constant buffer
        // -----------------------------
        ErodeCB erodeParams{};
        erodeParams.Width = W;
        erodeParams.Height = H;
        erodeParams.Talus = 0.005f;     // threshold slope (in normalized height units)
        erodeParams.Strength = 0.50f;   // max fraction moved per step (stability <= 1)

        auto cbErode = d3d::CreateConstantBuffer<ErodeCB>(device.Get());
        d3d::UpdateConstantBuffer(ctx.Get(), cbErode.Get(), erodeParams);

        // -----------------------------
        // Erosion iterations
        // -----------------------------
        const UINT TGX = 16, TGY = 16;
        const UINT groupsX = (W + TGX - 1) / TGX;
        const UINT groupsY = (H + TGY - 1) / TGY;
        const int iterations = 120;

        for (int it = 0; it < iterations; ++it)
        {
            // Pass 1: compute outflow from heightA -> outflowTex
            ctx->CSSetShader(csOutflow.Get(), nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, cbErode.GetAddressOf());

            ID3D11ShaderResourceView* srvs1[1] = { heightA_SRV.Get() };
            ctx->CSSetShaderResources(0, 1, srvs1);

            ID3D11UnorderedAccessView* uavs1[1] = { outflowUAV.Get() };
            UINT initCounts[1] = { 0 };
            ctx->CSSetUnorderedAccessViews(0, 1, uavs1, initCounts);

            ctx->Dispatch(groupsX, groupsY, 1);
            d3d::UnbindCSResources(ctx.Get(), 4, 2);

            // Pass 2: apply flows to produce heightB
            ctx->CSSetShader(csApply.Get(), nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, cbErode.GetAddressOf());

            ID3D11ShaderResourceView* srvs2[2] = { heightA_SRV.Get(), outflowSRV.Get() };
            ctx->CSSetShaderResources(0, 2, srvs2);

            ID3D11UnorderedAccessView* uavs2[1] = { heightB_UAV.Get() };
            ctx->CSSetUnorderedAccessViews(0, 1, uavs2, initCounts);

            ctx->Dispatch(groupsX, groupsY, 1);
            d3d::UnbindCSResources(ctx.Get(), 4, 2);

            // Swap A <-> B
            std::swap(heightA, heightB);
            std::swap(heightA_SRV, heightB_SRV);
            std::swap(heightA_UAV, heightB_UAV);
        }

        // -----------------------------
        // Read back heightmap
        // -----------------------------
        std::vector<float> eroded;
        d3d::ReadbackFloatTexture(device.Get(), ctx.Get(), heightA.Get(), W, H, eroded);
        SaveRaw("height_after_erosion.raw", eroded);
        SavePGM8("height_after_erosion.pgm", eroded, W, H);

        // -----------------------------
        // Poisson-disk "tree" spawn
        // -----------------------------
        auto sampleHeight = [&](float x, float y) -> float {
            return BilinearSample(eroded, W, H, x, y);
        };

        auto slopeMag = [&](float x, float y) -> float {
            float hC = sampleHeight(x, y);
            float hX = sampleHeight(std::min(x+1.0f, float(W-1)), y);
            float hY = sampleHeight(x, std::min(y+1.0f, float(H-1)));
            float dx = hX - hC;
            float dy = hY - hC;
            return sqrtf(dx*dx + dy*dy);
        };

        PoissonParams pp;
        pp.width = float(W);
        pp.height = float(H);
        pp.minDist = 8.0f;  // tweak as desired
        pp.k = 30;

        const float waterHeight = 0.30f; // reject low (water) areas
        const float maxSlope    = 0.060f; // reject steep slopes

        std::mt19937 rng(1337);
        auto accept = [&](float x, float y) -> bool {
            float h = sampleHeight(x, y);
            if (h < waterHeight) return false;
            if (slopeMag(x, y) > maxSlope) return false;
            return true;
        };

        std::vector<Float2> trees = PoissonSample(pp, rng, accept);

        // Save trees.csv (x,y,height)
        {
            std::ofstream out("trees.csv");
            out << "x,y,height\n";
            for (auto& t : trees)
                out << t.x << "," << t.y << "," << sampleHeight(t.x, t.y) << "\n";
        }

        std::cout << "Erosion done. Spawned " << trees.size() << " trees.\n";
        std::cout << "Wrote height_after_erosion.pgm, height_after_erosion.raw, trees.csv\n";
    }
    catch (std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR | MB_OK);
        return -1;
    }

    return 0;
}
