#pragma once
#include <Windows.h>  // For FormatMessageA, HRESULT helpers
#include <wrl/client.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <sstream>  // for HR_CHECK message formatting
#include <cstring>  // for std::memcpy

// Robust HRESULT -> exception helper with rich diagnostics.
namespace d3d
{
    inline std::runtime_error HrError(HRESULT hr, const char* expr, const char* file, int line)
    {
        // Prefer Win32 facility codes as raw DWORDs for FormatMessageA.
        DWORD code = (HRESULT_FACILITY(hr) == FACILITY_WIN32)
            ? static_cast<DWORD>(HRESULT_CODE(hr))
            : static_cast<DWORD>(hr);

        char sys[512] = {};
        DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD len = FormatMessageA(flags,
                                   nullptr,
                                   code,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   sys,
                                   static_cast<DWORD>(sizeof(sys)),
                                   nullptr);

        // If that failed, try again using the raw HRESULT in case it maps differently.
        if (len == 0)
        {
            len = FormatMessageA(flags,
                                 nullptr,
                                 static_cast<DWORD>(hr),
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 sys,
                                 static_cast<DWORD>(sizeof(sys)),
                                 nullptr);
        }

        // Trim trailing CR/LF/space/dot that FormatMessage often appends.
        while (len && (sys[len - 1] == '\r' || sys[len - 1] == '\n' || sys[len - 1] == ' ' || sys[len - 1] == '.'))
        {
            sys[--len] = '\0';
        }
        if (len == 0)
        {
            strcpy_s(sys, "Unrecognized error");
        }

        std::ostringstream oss;
        oss << expr
            << " failed. hr=0x"
            << std::hex << std::uppercase << static_cast<unsigned long>(hr)
            << " (" << std::dec << static_cast<long>(hr) << "): "
            << sys
            << " at " << file << ":" << line;
        return std::runtime_error(oss.str());
    }
}

#ifndef HR_CHECK
#define HR_CHECK(x) do {                                     \
    HRESULT _hr = (x);                                       \
    if (FAILED(_hr)) {                                       \
        throw d3d::HrError(_hr, #x, __FILE__, __LINE__);     \
    }                                                        \
} while(0)
#endif

using Microsoft::WRL::ComPtr;

namespace d3d
{
    // Compile an HLSL shader from file.
    ComPtr<ID3DBlob> CompileShaderFromFile(
        const std::wstring& path,
        const std::string& entry,
        const std::string& target,
        const D3D_SHADER_MACRO* defines = nullptr);

    // Create a 2D float (R32_FLOAT) texture with SRV + UAV.
    void CreateFloatTextureUAVSRV(
        ID3D11Device* device,
        uint32_t width,
        uint32_t height,
        ComPtr<ID3D11Texture2D>& tex,
        ComPtr<ID3D11ShaderResourceView>& srv,
        ComPtr<ID3D11UnorderedAccessView>& uav);

    // Create a 2D RGBA32F texture (for outflow) with SRV + UAV.
    void CreateRGBA32FTextureUAVSRV(
        ID3D11Device* device,
        uint32_t width,
        uint32_t height,
        ComPtr<ID3D11Texture2D>& tex,
        ComPtr<ID3D11ShaderResourceView>& srv,
        ComPtr<ID3D11UnorderedAccessView>& uav);

    // Update a R32_FLOAT texture from CPU floats (width*height elements).
    void UpdateFloatTexture(
        ID3D11DeviceContext* ctx,
        ID3D11Texture2D* tex,
        const float* data,
        uint32_t width,
        uint32_t height);

    // Readback a R32_FLOAT texture into CPU memory (width*height floats).
    void ReadbackFloatTexture(
        ID3D11Device* device,
        ID3D11DeviceContext* ctx,
        ID3D11Texture2D* gpuTex,
        uint32_t width,
        uint32_t height,
        std::vector<float>& outCPU);

    // Create a constant buffer (immutable size, frequently updated).
    template<typename T>
    ComPtr<ID3D11Buffer> CreateConstantBuffer(ID3D11Device* device)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (UINT)((sizeof(T) + 15) & ~15);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = 0;

        ComPtr<ID3D11Buffer> cb;
        HR_CHECK(device->CreateBuffer(&bd, nullptr, cb.GetAddressOf()));
        return cb;
    }

    // Helper to map/update a constant buffer.
    template<typename T>
    void UpdateConstantBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const T& value)
    {
        D3D11_MAPPED_SUBRESOURCE ms{};
        HR_CHECK(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        std::memcpy(ms.pData, &value, sizeof(T));
        ctx->Unmap(cb, 0);
    }

    // Unbind SRVs/UAVs to avoid hazards between passes.
    inline void UnbindCSResources(ID3D11DeviceContext* ctx, UINT srvCount = 8, UINT uavCount = 4)
    {
        std::vector<ID3D11ShaderResourceView*> nullSrvs(srvCount, nullptr);
        ctx->CSSetShaderResources(0, srvCount, nullSrvs.data());

        std::vector<ID3D11UnorderedAccessView*> nullUavs(uavCount, nullptr);
        UINT initialCounts[4] = {0,0,0,0};
        ctx->CSSetUnorderedAccessViews(0, uavCount, nullUavs.data(), initialCounts);
    }
}
