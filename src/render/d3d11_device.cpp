// d3d11_device.cpp (extract)
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;

struct D3D11Device {
  ComPtr<ID3D11Device>           dev;
  ComPtr<ID3D11DeviceContext>    ctx;
  ComPtr<IDXGISwapChain1>        swap;
  ComPtr<ID3D11RenderTargetView> rtv;
  DXGI_FORMAT backFmt = DXGI_FORMAT_B8G8R8A8_UNORM;

  static D3D11Device create(HWND hwnd, bool enableDebug) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    if (enableDebug) flags |= D3D11_CREATE_DEVICE_DEBUG; // debug layer in Debug builds
#endif
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;

    D3D_FEATURE_LEVEL fl;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, flags, nullptr, 0,
                      D3D11_SDK_VERSION, &dev, &fl, &ctx);

    // flip-model swap chain
    ComPtr<IDXGIDevice> dxgiDev; dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adp; dxgiDev->GetAdapter(&adp);
    ComPtr<IDXGIFactory2> fac; adp->GetParent(__uuidof(IDXGIFactory2), &fac);

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Format = backFmt;
    d.Width = 0; d.Height = 0; d.BufferCount = 2;
    d.SampleDesc = {1,0};
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain1> swap;
    fac->CreateSwapChainForHwnd(dev.Get(), hwnd, &d, nullptr, nullptr, &swap);

    // RTV
    ComPtr<ID3D11Texture2D> bb; swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(bb.Get(), nullptr, &rtv);

    D3D11Device out; out.dev=dev; out.ctx=ctx; out.swap=swap; out.rtv=rtv;
    return out;
  }
};
