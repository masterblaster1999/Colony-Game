#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "TerrainMesh.hpp"

namespace cg {

class TerrainRenderer {
public:
    bool initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                    const wchar_t* hlslPath = L"shaders/Terrain.hlsl");
    void upload(const TerrainMeshData& mesh);
    void render(const DirectX::XMMATRIX& mvp, const float lightDir[3]);

private:
    Microsoft::WRL::ComPtr<ID3D11Device>         device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  context_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>         vb_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>         ib_;
    UINT                                          indexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>   vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>    il_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>         cb_;

    bool compileShaders(const wchar_t* hlslPath);
};

} // namespace cg
