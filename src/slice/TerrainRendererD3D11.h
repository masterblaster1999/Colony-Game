// src/slice/TerrainRendererD3D11.h
#pragma once

/*
    TerrainRendererD3D11
    -------------------
    Patch4 split: owns the terrain + cube rendering resources and draw calls.

    - grid mesh + height texture
    - terrain shaders + constant buffers
    - cube shaders + constant buffers

    SliceRendererD3D11 keeps timing + global device state and forwards to this.
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

#include <cstdint>
#include <vector>

namespace slice {

class SliceSimulation;

class TerrainRendererD3D11 {
public:
    TerrainRendererD3D11() = default;

    void create(ID3D11Device* dev, ID3D11DeviceContext* ctx, const SliceSimulation& sim);
    void regenerateHeight(ID3D11Device* dev, const SliceSimulation& sim);

    void drawTerrain(ID3D11DeviceContext* ctx, const SliceSimulation& sim,
                     const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P);

    void drawCube(ID3D11DeviceContext* ctx, const SliceSimulation& sim,
                  const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P);

private:
    struct Vtx { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; };
    struct VtxN { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 nrm; };

    struct Mesh {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vbo;
        Microsoft::WRL::ComPtr<ID3D11Buffer> ibo;
        UINT indexCount{};
    };

    struct HeightTexture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        int W{}, H{};

        void create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt);
    };

    // Geometry
    Mesh grid_{};
    Mesh cube_{};
    HeightTexture heightTex_{};

    // Terrain pipeline
    Microsoft::WRL::ComPtr<ID3D11VertexShader> terrainVS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  terrainPS_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  terrainIL_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbCamera_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbTerrain_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampLinear_;

    // Cube pipeline
    Microsoft::WRL::ComPtr<ID3D11VertexShader> colorVS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  colorPS_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  colorIL_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbCameraCube_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbColor_;
};

} // namespace slice
