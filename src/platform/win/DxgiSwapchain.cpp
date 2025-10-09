// src/platform/win/DxgiSwapchain.cpp (creation excerpt)
DXGI_SWAP_CHAIN_DESC1 desc{};
desc.Width = width; desc.Height = height;
desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;      // backbuffer is *non*‑sRGB; apply gamma in post
desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
desc.BufferCount = 3;                          // triple buffering
desc.SampleDesc = {1,0};
desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
desc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

ComPtr<IDXGISwapChain1> sc1;
DX::ThrowIfFailed(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &desc, nullptr, nullptr, &sc1));
DX::ThrowIfFailed(sc1.As(&swapchain));
