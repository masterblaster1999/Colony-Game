#pragma once
// OrbitalRenderer.h - tiny D3D11 renderer for the orbital system.

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <DirectXMath.h>
#include <string>
#include "../space/OrbitalSystem.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace colony::space {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct OrbitalRendererOptions {
    bool drawStar = true;
    bool drawPlanets = true;
    bool drawMoons = true;
    bool drawOrbits = true;
    int  sphereSubdiv = 2;     // 0..3 (be mindful)
    float orbitLineWidth = 1.0f; // (DX11 fixed-function lines; width may be ignored)
};

class OrbitalRenderer {
public:
    bool Initialize(ID3D11Device* device, const std::wstring& shaderDir);
    void Shutdown();

    // Render everything in one go
    void Render(ID3D11DeviceContext* ctx,
                const OrbitalSystem& system,
                const XMMATRIX& view,
                const XMMATRIX& proj,
                const OrbitalRendererOptions& opt);

private:
    struct VertexPN {
        XMFLOAT3 pos;
        XMFLOAT3 nrm;
    };

    struct CameraCB {
        XMMATRIX viewProj;
        XMFLOAT3 lightDir; float time;
        XMFLOAT3 cameraPos; float _pad0;
    };
    struct ObjectCB {
        XMMATRIX world;
        XMFLOAT4 color;
    };

    struct Mesh {
        ComPtr<ID3D11Buffer> vb, ib;
        UINT indexCount = 0;
        UINT stride = sizeof(VertexPN);
    };

    struct OrbitVB {
        ComPtr<ID3D11Buffer> vb;
        UINT vertexCount = 0;
        XMFLOAT4 color{};
        int bodyIndex = -1;
        int parentIndex = -1;
    };

private:
    bool CreateIcoSphere(ID3D11Device* dev, int subdiv, Mesh& outMesh);
    bool CompileShaders(ID3D11Device* dev, const std::wstring& dir);

    // Utilities
    void DrawBody(ID3D11DeviceContext* ctx, const Mesh& sphere,
                  const XMMATRIX& world, const XMFLOAT4& color);

    void DrawOrbit(ID3D11DeviceContext* ctx, const OrbitVB& orbit,
                   const XMMATRIX& parentWorld, const XMFLOAT4& color);

    void RebuildOrbitBuffers(ID3D11Device* dev, const OrbitalSystem& system);

private:
    // GPU resources
    ComPtr<ID3D11VertexShader> m_vsSphere;
    ComPtr<ID3D11PixelShader>  m_psSphere;
    ComPtr<ID3D11InputLayout>  m_ilSphere;

    ComPtr<ID3D11VertexShader> m_vsLine;
    ComPtr<ID3D11PixelShader>  m_psLine;
    ComPtr<ID3D11InputLayout>  m_ilLine;

    ComPtr<ID3D11Buffer> m_cbCamera;
    ComPtr<ID3D11Buffer> m_cbObject;

    Mesh m_sphere;
    std::vector<OrbitVB> m_orbits;

    std::wstring m_shaderDir;
    bool m_ready = false;

    // cached for orbit buffer rebuild checks (simple approach)
    size_t m_cachedOrbitBodyCount = 0;
};

} // namespace colony::space
