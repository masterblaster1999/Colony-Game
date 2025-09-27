// OrbitalRenderer.cpp
#include "OrbitalRenderer.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cassert>

using namespace DirectX;

namespace colony::space {

// ----------------- Shader helpers -----------------

static bool CompileFromFile(const std::wstring& file, const char* entry,
                            const char* target, ComPtr<ID3DBlob>& outBlob,
                            const D3D_SHADER_MACRO* macros = nullptr) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompileFromFile(file.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, target, flags, 0, &outBlob, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    return true;
}

bool OrbitalRenderer::CompileShaders(ID3D11Device* dev, const std::wstring& dir) {
    m_shaderDir = dir;
    ComPtr<ID3DBlob> vs, ps, vsl, psl;

    // Sphere shaders
    if (!CompileFromFile(dir + L"\\OrbitalSphereVS.hlsl", "VSMain", "vs_5_0", vs)) return false;
    if (!CompileFromFile(dir + L"\\OrbitalSpherePS.hlsl", "PSMain", "ps_5_0", ps)) return false;
    if (FAILED(dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vsSphere))) return false;
    if (FAILED(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_psSphere))) return false;

    D3D11_INPUT_ELEMENT_DESC ils[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,   D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,  D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    if (FAILED(dev->CreateInputLayout(ils, _countof(ils),
        vs->GetBufferPointer(), vs->GetBufferSize(), &m_ilSphere))) return false;

    // Line shaders
    if (!CompileFromFile(dir + L"\\OrbitLineVS.hlsl", "VSMain", "vs_5_0", vsl)) return false;
    if (!CompileFromFile(dir + L"\\OrbitLinePS.hlsl", "PSMain", "ps_5_0", psl)) return false;
    if (FAILED(dev->CreateVertexShader(vsl->GetBufferPointer(), vsl->GetBufferSize(), nullptr, &m_vsLine))) return false;
    if (FAILED(dev->CreatePixelShader(psl->GetBufferPointer(), psl->GetBufferSize(), nullptr, &m_psLine))) return false;

    D3D11_INPUT_ELEMENT_DESC ill[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,   D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    if (FAILED(dev->CreateInputLayout(ill, _countof(ill),
        vsl->GetBufferPointer(), vsl->GetBufferSize(), &m_ilLine))) return false;

    // Constant buffers
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbd.ByteWidth = sizeof(CameraCB);
    if (FAILED(dev->CreateBuffer(&cbd, nullptr, &m_cbCamera))) return false;

    cbd.ByteWidth = sizeof(ObjectCB);
    if (FAILED(dev->CreateBuffer(&cbd, nullptr, &m_cbObject))) return false;

    return true;
}

// ----------------- Mesh generation (icosphere) -----------------

static void Subdivide(std::vector<XMFLOAT3>& vtx, std::vector<uint32_t>& idx) {
    struct EdgeKey { uint32_t a,b; bool operator==(const EdgeKey& o)const{return a==o.a&&b==o.b;} };
    struct Hasher { size_t operator()(const EdgeKey& k) const { return (size_t(k.a)<<32) ^ size_t(k.b); } };
    std::unordered_map<EdgeKey,uint32_t,Hasher> mid;

    auto midpoint = [&](uint32_t i0, uint32_t i1)->uint32_t{
        EdgeKey k{ std::min(i0,i1), std::max(i0,i1) };
        auto it = mid.find(k);
        if (it!=mid.end()) return it->second;
        XMFLOAT3 a = vtx[i0], b=vtx[i1];
        XMFLOAT3 m{ (a.x+b.x)*0.5f, (a.y+b.y)*0.5f, (a.z+b.z)*0.5f };
        XMVECTOR mm = XMVector3Normalize(XMLoadFloat3(&m));
        XMStoreFloat3(&m, mm);
        uint32_t idxNew = (uint32_t)vtx.size();
        vtx.push_back(m);
        mid[k]=idxNew;
        return idxNew;
    };

    std::vector<uint32_t> out;
    out.reserve(idx.size()*4);
    for (size_t i=0;i<idx.size();i+=3) {
        uint32_t i0=idx[i], i1=idx[i+1], i2=idx[i+2];
        uint32_t a = midpoint(i0,i1);
        uint32_t b = midpoint(i1,i2);
        uint32_t c = midpoint(i2,i0);
        out.insert(out.end(), { i0,a,c,  a,i1,b,  c,b,i2,  a,b,c });
    }
    idx.swap(out);
}

bool OrbitalRenderer::CreateIcoSphere(ID3D11Device* dev, int subdiv, Mesh& out) {
    // 12 verts of an icosahedron
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<XMFLOAT3> pos = {
        {-1, t, 0},{1, t, 0},{-1,-t,0},{1,-t,0},
        {0,-1, t},{0, 1, t},{0,-1,-t},{0, 1,-t},
        { t, 0,-1},{ t, 0, 1},{-t,0,-1},{-t,0, 1}
    };
    for (auto& p: pos) {
        XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&p));
        XMStoreFloat3(&p, v);
    }
    std::vector<uint32_t> idx = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1
    };

    subdiv = std::max(0, std::min(subdiv, 4));
    for (int s=0; s<subdiv; ++s) Subdivide(pos, idx);

    // Build vertex buffer with normals=positions
    std::vector<VertexPN> verts;
    verts.reserve(pos.size());
    for (auto& p: pos) verts.push_back({p, p});

    D3D11_BUFFER_DESC vbd{};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(VertexPN));
    D3D11_SUBRESOURCE_DATA vsd{ verts.data() };
    if (FAILED(dev->CreateBuffer(&vbd, &vsd, &out.vb))) return false;

    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
    D3D11_SUBRESOURCE_DATA isd{ idx.data() };
    if (FAILED(dev->CreateBuffer(&ibd, &isd, &out.ib))) return false;

    out.indexCount = (UINT)idx.size();
    return true;
}

// ----------------- Initialize/Shutdown -----------------

bool OrbitalRenderer::Initialize(ID3D11Device* device, const std::wstring& shaderDir) {
    if (!CompileShaders(device, shaderDir)) return false;
    if (!CreateIcoSphere(device, 2, m_sphere)) return false;

    // Constant buffers exist; input layouts exist.
    m_ready = true;
    return true;
}

void OrbitalRenderer::Shutdown() {
    m_ready = false;
    m_ilSphere.Reset(); m_vsSphere.Reset(); m_psSphere.Reset();
    m_ilLine.Reset(); m_vsLine.Reset(); m_psLine.Reset();
    m_cbCamera.Reset(); m_cbObject.Reset();
    m_sphere = {};
    m_orbits.clear();
    m_cachedOrbitBodyCount = 0;
}

// ----------------- Render -----------------

void OrbitalRenderer::RebuildOrbitBuffers(ID3D11Device* dev, const OrbitalSystem& system) {
    m_orbits.clear();
    auto& lines = system.OrbitLines();
    m_orbits.reserve(lines.size());
    for (auto& l : lines) {
        OrbitVB orb{};
        orb.bodyIndex = l.bodyIndex;
        orb.parentIndex = l.parentIndex;
        orb.color = XMFLOAT4(l.color.r, l.color.g, l.color.b, l.color.a);
        orb.vertexCount = (UINT)l.points.size();

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)(l.points.size() * sizeof(XMFLOAT3));
        D3D11_SUBRESOURCE_DATA srd{ l.points.data() };
        if (FAILED(dev->CreateBuffer(&bd, &srd, &orb.vb))) continue;

        m_orbits.push_back(std::move(orb));
    }
    m_cachedOrbitBodyCount = system.Bodies().size();
}

void OrbitalRenderer::DrawBody(ID3D11DeviceContext* ctx, const Mesh& sphere,
                               const XMMATRIX& world, const XMFLOAT4& color) {
    // Object constants
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(m_cbObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        ObjectCB* cb = reinterpret_cast<ObjectCB*>(ms.pData);
        cb->world = XMMatrixTranspose(world);
        cb->color = color;
        ctx->Unmap(m_cbObject.Get(), 0);
    }
    UINT stride = sphere.stride, offset = 0;
    ID3D11Buffer* vb = sphere.vb.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(sphere.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(m_ilSphere.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_vsSphere.Get(), nullptr, 0);
    ctx->PSSetShader(m_psSphere.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { m_cbCamera.Get(), m_cbObject.Get() };
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ctx->DrawIndexed(sphere.indexCount, 0, 0);
}

void OrbitalRenderer::DrawOrbit(ID3D11DeviceContext* ctx, const OrbitVB& orbit,
                                const XMMATRIX& parentWorld, const XMFLOAT4& color) {
    // Object constants (world is parent transform; orbit VB is in local coords)
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(m_cbObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        ObjectCB* cb = reinterpret_cast<ObjectCB*>(ms.pData);
        cb->world = XMMatrixTranspose(parentWorld);
        cb->color = color;
        ctx->Unmap(m_cbObject.Get(), 0);
    }
    UINT stride = sizeof(XMFLOAT3), offset = 0;
    ID3D11Buffer* vb = orbit.vb.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetInputLayout(m_ilLine.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    ctx->VSSetShader(m_vsLine.Get(), nullptr, 0);
    ctx->PSSetShader(m_psLine.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { m_cbCamera.Get(), m_cbObject.Get() };
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ctx->Draw(orbit.vertexCount, 0);
}

void OrbitalRenderer::Render(ID3D11DeviceContext* ctx,
                             const OrbitalSystem& system,
                             const XMMATRIX& view,
                             const XMMATRIX& proj,
                             const OrbitalRendererOptions& opt) {
    if (!m_ready) return;

    // Rebuild orbit VBs if body count changed (simple heuristic)
    // (If you change elements at runtime, you may want a hash to detect changes)
    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (m_cachedOrbitBodyCount != system.Bodies().size()) {
        RebuildOrbitBuffers(dev.Get(), system);
    }

    // Camera constants
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(m_cbCamera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        CameraCB* cb = reinterpret_cast<CameraCB*>(ms.pData);
        cb->viewProj = XMMatrixTranspose(view * proj);
        cb->lightDir = XMFLOAT3(0.25f, -0.6f, 0.7f);
        cb->time = 0.0f;
        cb->cameraPos = XMFLOAT3(0,0,0); // if you want specular later
        ctx->Unmap(m_cbCamera.Get(), 0);
    }

    // World transforms & draw
    const auto& bodies = system.Bodies();
    const auto& scale = system.Scale();

    // Draw star/planets/moons
    for (size_t i=0; i<bodies.size(); ++i) {
        const auto& b = bodies[i];

        if (b.type == BodyType::Star && !opt.drawStar) continue;
        if (b.type == BodyType::Planet && !opt.drawPlanets) continue;
        if (b.type == BodyType::Moon && !opt.drawMoons) continue;

        // Build world matrix from position (km->units) and radius scale
        float x = (float)(b.worldPosKm.x * scale.kmToUnits);
        float y = (float)(b.worldPosKm.y * scale.kmToUnits);
        float z = (float)(b.worldPosKm.z * scale.kmToUnits);

        float rUnits = (float)(b.radiusKm * scale.kmToUnits * scale.radiusScale);

        XMMATRIX S = XMMatrixScaling(rUnits, rUnits, rUnits);
        XMMATRIX T = XMMatrixTranslation(x, y, z);
        XMMATRIX W = S * T;

        XMFLOAT4 color = XMFLOAT4(b.color.r, b.color.g, b.color.b, b.color.a);
        DrawBody(ctx, m_sphere, W, color);
    }

    // Draw orbits (planets around star: parent world = identity; moons use parent position)
    if (opt.drawOrbits) {
        for (auto& o : m_orbits) {
            XMMATRIX parentWorld = XMMatrixIdentity();
            if (o.parentIndex >= 0) {
                const auto& parent = bodies[(size_t)o.parentIndex];
                float px = (float)(parent.worldPosKm.x * scale.kmToUnits);
                float py = (float)(parent.worldPosKm.y * scale.kmToUnits);
                float pz = (float)(parent.worldPosKm.z * scale.kmToUnits);
                parentWorld = XMMatrixTranslation(px, py, pz);
            }
            DrawOrbit(ctx, o, parentWorld, o.color);
        }
    }
}

} // namespace colony::space
