// demos/erosion/ErosionDemo.cpp
// Windows-only D3D11 compute demo that runs a diffusion-like "thermal erosion"
// on a heightfield and writes a grayscale PGM image (P5) as output.

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

static void CheckHR(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s failed (hr=0x%08X)", what, (unsigned)hr);
        throw std::runtime_error(buf);
    }
}

struct Cmd
{
    uint32_t w = 512;
    uint32_t h = 512;
    uint32_t iters = 256;
    float    strength = 0.20f; // stable <= 0.25 (4-neighbor)
    float    talus = 0.0f;     // small deadzone (0..0.02)
    std::filesystem::path out = L"erosion_out.pgm";
};

static Cmd ParseArgs(int argc, wchar_t** argv)
{
    Cmd c;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view a = argv[i];
        auto next = [&]() -> std::wstring_view {
            if (i + 1 >= argc) throw std::runtime_error("missing value for argument");
            return std::wstring_view(argv[++i]);
        };
        if (a == L"--w") c.w = (uint32_t)std::stoul(std::wstring(next()));
        else if (a == L"--h") c.h = (uint32_t)std::stoul(std::wstring(next()));
        else if (a == L"--iters") c.iters = (uint32_t)std::stoul(std::wstring(next()));
        else if (a == L"--strength") c.strength = std::stof(std::wstring(next()));
        else if (a == L"--talus") c.talus = std::stof(std::wstring(next()));
        else if (a == L"--out") c.out = next();
        else if (a == L"--help" || a == L"-h")
        {
            std::wcout << L"Usage: ColonyComputeErosion.exe [--w N] [--h N] [--iters N] "
                          L"[--strength S] [--talus T] [--out file.pgm]\n";
            std::exit(0);
        }
        else
        {
            std::wcerr << L"Unknown arg: " << a << L"\n";
            std::exit(2);
        }
    }
    c.w = std::max(8u, c.w);
    c.h = std::max(8u, c.h);
    c.strength = std::clamp(c.strength, 0.0f, 0.25f);
    c.talus = std::clamp(c.talus, 0.0f, 0.1f);
    return c;
}

struct ErosionParamsCB
{
    UINT width;
    UINT height;
    float strength;
    float talus;
};

// Simple, deterministic test heightfield (radial falloff + trig ripple)
static std::vector<float> MakeInitialField(uint32_t w, uint32_t h)
{
    std::vector<float> f(w * h);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            float fx = (float(x) / float(w - 1)) * 2.0f - 1.0f;
            float fy = (float(y) / float(h - 1)) * 2.0f - 1.0f;
            float r  = std::sqrt(fx * fx + fy * fy);
            float ripple = 0.5f * std::sin(6.28318f * (fx * 0.75f)) * std::cos(6.28318f * (fy * 0.5f));
            float base = 0.8f - r + 0.2f * ripple;
            f[y * w + x] = std::clamp(base * 0.5f + 0.5f, 0.0f, 1.0f);
        }
    }
    return f;
}

static void WritePGM(const std::filesystem::path& path, const float* data, uint32_t w, uint32_t h)
{
    // Normalize to 0..255 for visualization
    float mn = data[0], mx = data[0];
    for (size_t i = 0; i < size_t(w) * h; ++i) { mn = std::min(mn, data[i]); mx = std::max(mx, data[i]); }
    float scale = (mx > mn) ? 255.0f / (mx - mn) : 1.0f;
    std::vector<uint8_t> bytes(w * h);
    for (size_t i = 0; i < size_t(w) * h; ++i)
    {
        float v = (data[i] - mn) * scale;
        bytes[i] = (uint8_t)std::clamp((int)std::lround(v), 0, 255);
    }

    std::filesystem::create_directories(path.parent_path());
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) throw std::runtime_error("failed to open output PGM");

    std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    std::fwrite(header.data(), 1, header.size(), f);
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Cmd cmd = ParseArgs(argc, argv);
        std::wcout << L"[ColonyComputeErosion] size=" << cmd.w << L"x" << cmd.h
                   << L" iters=" << cmd.iters << L" strength=" << cmd.strength
                   << L" talus=" << cmd.talus << L"\n";

        // --------------------------------------------------------------------
        // D3D11 device
        // --------------------------------------------------------------------
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #if _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> ctx;
        D3D_FEATURE_LEVEL flOut = {};
        CheckHR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                  flags, levels, _countof(levels),
                                  D3D11_SDK_VERSION, &device, &flOut, &ctx),
                "D3D11CreateDevice");

        // --------------------------------------------------------------------
        // Textures (ping-pong), SRVs/UAVs
        // --------------------------------------------------------------------
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = cmd.w;
        td.Height = cmd.h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        ComPtr<ID3D11Texture2D> texA, texB;
        CheckHR(device->CreateTexture2D(&td, nullptr, &texA), "CreateTexture2D A");
        CheckHR(device->CreateTexture2D(&td, nullptr, &texB), "CreateTexture2D B");

        // Upload initial data to A
        auto init = MakeInitialField(cmd.w, cmd.h);
        ctx->UpdateSubresource(texA.Get(), 0, nullptr, init.data(), UINT(cmd.w * sizeof(float)), 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MostDetailedMip = 0;
        srvd.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> srvA, srvB;
        CheckHR(device->CreateShaderResourceView(texA.Get(), &srvd, &srvA), "CreateSRV A");
        CheckHR(device->CreateShaderResourceView(texB.Get(), &srvd, &srvB), "CreateSRV B");

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format = td.Format;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavd.Texture2D.MipSlice = 0;

        ComPtr<ID3D11UnorderedAccessView> uavA, uavB;
        CheckHR(device->CreateUnorderedAccessView(texA.Get(), &uavd, &uavA), "CreateUAV A");
        CheckHR(device->CreateUnorderedAccessView(texB.Get(), &uavd, &uavB), "CreateUAV B");

        // --------------------------------------------------------------------
        // Constant buffer
        // --------------------------------------------------------------------
        ErosionParamsCB params = { cmd.w, cmd.h, cmd.strength, cmd.talus };

        D3D11_BUFFER_DESC cbd = {};
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.ByteWidth = sizeof(ErosionParamsCB);
        cbd.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Buffer> cb;
        CheckHR(device->CreateBuffer(&cbd, nullptr, &cb), "CreateBuffer(CB)");
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &params, 0, 0);

        // --------------------------------------------------------------------
        // Compute shader (load .cso copied by CMake POST_BUILD step)
        // --------------------------------------------------------------------
        std::filesystem::path exeDir = std::filesystem::path(argv[0]).parent_path();
        std::filesystem::path csoPath = exeDir / "ErosionCS.cso";

        ComPtr<ID3DBlob> csBlob;
        if (FAILED(D3DReadFileToBlob(csoPath.c_str(), &csBlob)))
        {
            // Try secondary location (build subdir) if run from a different CWD
            std::filesystem::path alt = std::filesystem::path(exeDir).parent_path() / "demos" / "erosion" / "ErosionCS.cso";
            CheckHR(D3DReadFileToBlob(alt.c_str(), &csBlob), "D3DReadFileToBlob(ErosionCS.cso)");
            csoPath = alt;
        }

        ComPtr<ID3D11ComputeShader> cs;
        CheckHR(device->CreateComputeShader(csBlob->GetBufferPointer(),
                                            csBlob->GetBufferSize(), nullptr, &cs),
                "CreateComputeShader");

        // --------------------------------------------------------------------
        // Run iterations (ping-pong between A and B)
        // --------------------------------------------------------------------
        auto bindAndDispatch = [&](ID3D11ShaderResourceView* srvSrc,
                                   ID3D11UnorderedAccessView* uavDst)
        {
            UINT nullUAV = 0;
            ctx->CSSetShader(cs.Get(), nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
            ctx->CSSetShaderResources(0, 1, &srvSrc);
            ctx->CSSetUnorderedAccessViews(0, 1, &uavDst, &nullUAV);

            UINT gx = (cmd.w + 7u) / 8u;
            UINT gy = (cmd.h + 7u) / 8u;
            ctx->Dispatch(gx, gy, 1);

            // Unbind to avoid hazards
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
            ctx->CSSetShaderResources(0, 1, nullSRV);
            ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, &nullUAV);
        };

        bool srcIsA = true;
        for (uint32_t i = 0; i < cmd.iters; ++i)
        {
            if (srcIsA) bindAndDispatch(srvA.Get(), uavB.Get());
            else        bindAndDispatch(srvB.Get(), uavA.Get());
            srcIsA = !srcIsA;
        }

        // Final source after even/odd swap
        ComPtr<ID3D11Texture2D> finalTex = srcIsA ? texA : texB;

        // --------------------------------------------------------------------
        // Read back to CPU and write PGM
        // --------------------------------------------------------------------
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> staging;
        CheckHR(device->CreateTexture2D(&sd, nullptr, &staging), "CreateTexture2D(staging)");
        ctx->CopyResource(staging.Get(), finalTex.Get());

        D3D11_MAPPED_SUBRESOURCE map = {};
        CheckHR(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Map(staging)");

        std::vector<float> out(cmd.w * cmd.h);
        for (uint32_t y = 0; y < cmd.h; ++y)
        {
            const float* row = reinterpret_cast<const float*>(
                static_cast<const uint8_t*>(map.pData) + y * map.RowPitch);
            std::copy_n(row, cmd.w, &out[y * cmd.w]);
        }
        ctx->Unmap(staging.Get(), 0);

        WritePGM(cmd.out, out.data(), cmd.w, cmd.h);
        std::wcout << L"Wrote: " << cmd.out.wstring() << L"\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
