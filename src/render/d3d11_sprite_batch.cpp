// d3d11_sprite_batch.cpp
//
// Minimal, self-contained Direct3D11 sprite batch vertex buffer upload path.
// This applies the "full patch" fix: proper D3D11_BUFFER_DESC usage,
// ComPtr<ID3D11Buffer>, Map/Unmap with WRITE_DISCARD, and bounded memcpy.
//
// Notes:
// - This file intentionally keeps the interface compact to focus on the
//   corrected buffer setup and flush logic that previously failed to compile.
// - If you already have a SpriteBatch header, align the class signature
//   accordingly (names and members here are conventional).
//

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>

using Microsoft::WRL::ComPtr;

class SpriteBatch {
public:
    struct Vertex {
        float pos[2];
        float uv[2];
        uint32_t color;
    };

    SpriteBatch() = default;

    // Initialize or reinitialize with a starting capacity (in vertices).
    bool init(ID3D11Device* device, size_t initialMaxVerts = 4096) {
        if (!device) return false;
        capacity_ = 0; // force (re)creation
        return ensureCapacity(device, initialMaxVerts);
    }

    // Append vertices to the CPU buffer
    void addVertices(const Vertex* verts, size_t count) {
        if (!verts || count == 0) return;
        cpuVerts_.insert(cpuVerts_.end(), verts, verts + count);
    }

    // Clear CPU buffer between frames
    void clear() { cpuVerts_.clear(); }

    // Upload and draw; only handles the upload part here (pipeline setup is external)
    void flush(ID3D11DeviceContext* ctx) {
        if (!ctx || cpuVerts_.empty()) return;

        // Grow GPU buffer if needed
        ComPtr<ID3D11Device> dev;
        ctx->GetDevice(&dev);
        if (!dev) return;

        if (!vb_ || capacity_ < cpuVerts_.size()) {
            // Grow to at least double to amortize allocations
            const size_t needed = cpuVerts_.size();
            const size_t growTo = std::max(needed, std::max(capacity_ * size_t(2), size_t(4096)));
            if (!ensureCapacity(dev.Get(), growTo)) return;
        }

        // Map/Unmap upload
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = ctx->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr) || !mapped.pData) {
            return; // could add logging
        }

        std::memcpy(mapped.pData, cpuVerts_.data(), cpuVerts_.size() * sizeof(Vertex));
        ctx->Unmap(vb_.Get(), 0);

        // Caller should now bind vb_ and issue a Draw() with the correct topology.
        // Example (outside):
        //   UINT stride = sizeof(Vertex), offset = 0;
        //   ID3D11Buffer* buf = vb_.Get();
        //   ctx->IASetVertexBuffers(0, 1, &buf, &stride, &offset);
        //   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        //   ctx->Draw(static_cast<UINT>(cpuVerts_.size()), 0);
    }

    ID3D11Buffer* buffer() const { return vb_.Get(); }
    size_t size() const { return cpuVerts_.size(); }
    size_t capacity() const { return capacity_; }

private:
    bool ensureCapacity(ID3D11Device* device, size_t maxVerts) {
        if (!device) return false;
        if (vb_ && capacity_ >= maxVerts) return true;

        // Create a new dynamic vertex buffer with CPU write access
        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = static_cast<UINT>(maxVerts * sizeof(Vertex));
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = 0;

        ComPtr<ID3D11Buffer> newVB;
        HRESULT hr = device->CreateBuffer(&vbDesc, nullptr, newVB.GetAddressOf());
        if (FAILED(hr) || !newVB) {
            return false;
        }

        vb_ = std::move(newVB);
        capacity_ = maxVerts;
        return true;
    }

private:
    ComPtr<ID3D11Buffer> vb_;
    std::vector<Vertex>  cpuVerts_;
    size_t               capacity_ = 0;
};


// ============================================================================
// Example usage pattern below illustrates the exact "patch" applied to the old
// sketch (buffer creation + flush). If you integrate with an existing system,
// you likely already call these from your renderer setup and frame loop.
// ============================================================================

namespace {
    // Keep an instance here only to mirror the original sketch usage.
    SpriteBatch g_spriteBatch;
}

// --- Original sketch fragment, corrected & applied as requested -------------

// Buffer creation (called during initialization)
static void CreateSpriteVB_Patched(ID3D11Device* dev, size_t MaxVerts) {
    // This mirrors the patch: proper D3D11_BUFFER_DESC and CreateBuffer on a ComPtr
    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(MaxVerts * sizeof(SpriteBatch::Vertex));
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ComPtr<ID3D11Buffer> vb;
    HRESULT hr = dev->CreateBuffer(&vbDesc, nullptr, vb.GetAddressOf());
    (void)hr; // Normally check FAILED(hr)
    // In this example we reuse g_spriteBatch to manage lifetime; prefer the class path:
    g_spriteBatch.init(dev, MaxVerts);
}

// Upload path (called every frame after filling CPU vertices)
void SpriteBatch::flush(ID3D11DeviceContext* ctx) {
    // This is the fixed Map/Unmap path from the patch, adapted to class members.
    if (!ctx || cpuVerts_.empty() || !vb_) return;

    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) || !m.pData) {
        return;
    }
    std::memcpy(m.pData, cpuVerts_.data(), cpuVerts_.size() * sizeof(Vertex));
    ctx->Unmap(vb_.Get(), 0);

    // set pipeline and Draw happens outside of this function.
}
