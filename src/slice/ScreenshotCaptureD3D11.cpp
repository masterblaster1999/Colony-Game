// src/slice/ScreenshotCaptureD3D11.cpp

#include "ScreenshotCaptureD3D11.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <vector>

#include <wrl/client.h>

namespace slice {

using Microsoft::WRL::ComPtr;

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

bool ScreenshotCaptureD3D11::saveBackbufferBMP(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t name[256];
    swprintf_s(name, L"Screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    ComPtr<ID3D11Texture2D> back;
    HR(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()));

    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    HR(dev->CreateTexture2D(&sdesc, nullptr, staging.GetAddressOf()));

    ctx->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;

    std::ofstream f(name, std::ios::binary);
    if (!f) {
        ctx->Unmap(staging.Get(), 0);
        return false;
    }

    const UINT W = desc.Width;
    const UINT H = desc.Height;
    const UINT rowSize = W * 4;

#pragma pack(push,1)
    struct BMPFileHeader {
        uint16_t bfType{ 0x4D42 };
        uint32_t bfSize;
        uint16_t r1{ 0 };
        uint16_t r2{ 0 };
        uint32_t bfOffBits{ 54 };
    };
    struct BMPInfoHeader {
        uint32_t biSize{ 40 };
        int32_t biWidth;
        int32_t biHeight;
        uint16_t biPlanes{ 1 };
        uint16_t biBitCount{ 32 };
        uint32_t biCompression{ 0 };
        uint32_t biSizeImage;
        int32_t biXPelsPerMeter{ 2835 };
        int32_t biYPelsPerMeter{ 2835 };
        uint32_t biClrUsed{ 0 };
        uint32_t biClrImportant{ 0 };
    };
#pragma pack(pop)

    BMPFileHeader fh{};
    fh.bfSize = 54 + rowSize * H;

    BMPInfoHeader ih{};
    ih.biWidth = (int32_t)W;
    ih.biHeight = -(int32_t)H; // top-down
    ih.biSizeImage = rowSize * H;

    f.write(reinterpret_cast<char*>(&fh), sizeof(fh));
    f.write(reinterpret_cast<char*>(&ih), sizeof(ih));

    std::vector<uint8_t> row(rowSize);
    for (UINT y = 0; y < H; ++y) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>((const uint8_t*)ms.pData + y * ms.RowPitch);

        for (UINT x = 0; x < W; ++x) {
            row[x * 4 + 0] = src[x * 4 + 2]; // B
            row[x * 4 + 1] = src[x * 4 + 1]; // G
            row[x * 4 + 2] = src[x * 4 + 0]; // R
            row[x * 4 + 3] = src[x * 4 + 3]; // A
        }
        f.write((char*)row.data(), rowSize);
    }

    ctx->Unmap(staging.Get(), 0);
    return true;
}

} // namespace slice
