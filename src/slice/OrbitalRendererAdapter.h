// src/slice/OrbitalRendererAdapter.h
#pragma once

/*
    OrbitalRendererAdapter
    ---------------------
    Patch4 split: thin glue around colony::space::OrbitalRenderer.

    Owns:
      - OrbitalRenderer (GPU resources)
      - alpha blend state used for the orbital overlay

    SliceRendererD3D11 forwards calls here.
*/

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

#include "render/OrbitalRenderer.h"

namespace slice {

class SliceSimulation;

class OrbitalRendererAdapter {
public:
    OrbitalRendererAdapter() = default;

    void create(ID3D11Device* dev);
    void reload(ID3D11Device* dev);

    void draw(ID3D11DeviceContext* ctx, const SliceSimulation& sim,
              const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P);

private:
    colony::space::OrbitalRenderer orender_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendAlpha_;
};

} // namespace slice
