//
// src/renderer/RenderGraph.cpp
//

#include "RenderGraph.h"

#include <algorithm>
#include <deque>
#include <sstream>

namespace cg::rendergraph {

  // -----------------------------
  // Small helpers
  // -----------------------------
  static std::wstring toWideAscii_(const std::string& s)
  {
    // Good enough for debug names (ASCII). If you need UTF-8 correctness, swap to MultiByteToWideChar.
    return std::wstring(s.begin(), s.end());
  }

  static D3D12_RESOURCE_BARRIER makeTransition_(ID3D12Resource* r,
                                                D3D12_RESOURCE_STATES before,
                                                D3D12_RESOURCE_STATES after)
  {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
  }

  static D3D12_RESOURCE_BARRIER makeUavBarrier_(ID3D12Resource* r)
  {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.UAV.pResource = r;
    return b;
  }

  // -----------------------------
  // TextureDesc factories
  // -----------------------------
  TextureDesc TextureDesc::Tex2D(UINT width, UINT height, DXGI_FORMAT format,
                                 D3D12_RESOURCE_FLAGS flags,
                                 UINT16 mipLevels, UINT16 arraySize,
                                 UINT sampleCount, UINT sampleQuality)
  {
    TextureDesc out{};
    out.desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    out.desc.Alignment = 0;
    out.desc.Width = width;
    out.desc.Height = height;
    out.desc.DepthOrArraySize = arraySize;
    out.desc.MipLevels = mipLevels;
    out.desc.Format = format;
    out.desc.SampleDesc.Count = sampleCount;
    out.desc.SampleDesc.Quality = sampleQuality;
    out.desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    out.desc.Flags = flags;
    return out;
  }

  TextureDesc TextureDesc::RenderTarget2D(UINT width, UINT height, DXGI_FORMAT format,
                                          const float* clearColorRGBA4,
                                          UINT16 mipLevels, UINT16 arraySize,
                                          UINT sampleCount, UINT sampleQuality)
  {
    TextureDesc out = Tex2D(width, height, format,
                            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                            mipLevels, arraySize, sampleCount, sampleQuality);

    if (clearColorRGBA4) {
      D3D12_CLEAR_VALUE cv{};
      cv.Format = format;
      cv.Color[0] = clearColorRGBA4[0];
      cv.Color[1] = clearColorRGBA4[1];
      cv.Color[2] = clearColorRGBA4[2];
      cv.Color[3] = clearColorRGBA4[3];
      out.clearValue = cv;
    }
    return out;
  }

  TextureDesc TextureDesc::DepthStencil2D(UINT width, UINT height, DXGI_FORMAT format,
                                          float clearDepth, uint8_t clearStencil,
                                          UINT sampleCount, UINT sampleQuality)
  {
    TextureDesc out = Tex2D(width, height, format,
                            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                            /*mips*/ 1, /*array*/ 1, sampleCount, sampleQuality);

    D3D12_CLEAR_VALUE cv{};
    cv.Format = format;
    cv.DepthStencil.Depth = clearDepth;
    cv.DepthStencil.Stencil = clearStencil;
    out.clearValue = cv;
    return out;
  }

  // -----------------------------
  // BufferDesc factory
  // -----------------------------
  BufferDesc BufferDesc::Buffer(uint64_t byteSize, D3D12_RESOURCE_FLAGS flags, uint64_t alignment)
  {
    BufferDesc out{};
    out.desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    out.desc.Alignment = alignment;
    out.desc.Width = byteSize;
    out.desc.Height = 1;
    out.desc.DepthOrArraySize = 1;
    out.desc.MipLevels = 1;
    out.desc.Format = DXGI_FORMAT_UNKNOWN;
    out.desc.SampleDesc.Count = 1;
    out.desc.SampleDesc.Quality = 0;
    out.desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    out.desc.Flags = flags;
    return out;
  }

  // -----------------------------
  // RenderGraphResources
  // -----------------------------
  ID3D12Resource* RenderGraphResources::texture(TextureHandle h) const
  {
    return rg_->getTexture_(h);
  }

  ID3D12Resource* RenderGraphResources::buffer(BufferHandle h) const
  {
    return rg_->getBuffer_(h);
  }

  const D3D12_RESOURCE_DESC& RenderGraphResources::textureDesc(TextureHandle h) const
  {
    // Safe: handle validated during compile.
    return rg_->textures_[h.id].d3dDesc;
  }

  const D3D12_RESOURCE_DESC& RenderGraphResources::bufferDesc(BufferHandle h) const
  {
    return rg_->buffers_[h.id].d3dDesc;
  }

  // -----------------------------
  // RenderGraph public
  // -----------------------------
  void RenderGraph::clear()
  {
    textures_.clear();
    buffers_.clear();
    passes_.clear();
    executionOrder_.clear();
    compiled_ = false;
  }

  TextureHandle RenderGraph::createTexture(std::string name, const TextureDesc& desc, D3D12_RESOURCE_STATES initialState)
  {
    TextureRes r{};
    r.name = std::move(name);
    r.imported = false;
    r.createDesc = desc;
    r.initialState = initialState;
    r.currentState = initialState;
    r.lastAccess = Access::Read;
    r.lastWasUav = false;

    textures_.push_back(std::move(r));
    compiled_ = false;
    return TextureHandle{ static_cast<uint32_t>(textures_.size() - 1) };
  }

  BufferHandle RenderGraph::createBuffer(std::string name, const BufferDesc& desc, D3D12_RESOURCE_STATES initialState)
  {
    BufferRes r{};
    r.name = std::move(name);
    r.imported = false;
    r.createDesc = desc;
    r.initialState = initialState;
    r.currentState = initialState;
    r.lastAccess = Access::Read;
    r.lastWasUav = false;

    buffers_.push_back(std::move(r));
    compiled_ = false;
    return BufferHandle{ static_cast<uint32_t>(buffers_.size() - 1) };
  }

  TextureHandle RenderGraph::importTexture(std::string name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState)
  {
    TextureRes r{};
    r.name = std::move(name);
    r.imported = true;
    r.external = resource;
    r.initialState = initialState;
    r.currentState = initialState;
    r.lastAccess = Access::Read;
    r.lastWasUav = false;

    if (resource) {
      r.d3dDesc = resource->GetDesc();
    }

    textures_.push_back(std::move(r));
    compiled_ = false;
    return TextureHandle{ static_cast<uint32_t>(textures_.size() - 1) };
  }

  BufferHandle RenderGraph::importBuffer(std::string name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState)
  {
    BufferRes r{};
    r.name = std::move(name);
    r.imported = true;
    r.external = resource;
    r.initialState = initialState;
    r.currentState = initialState;
    r.lastAccess = Access::Read;
    r.lastWasUav = false;

    if (resource) {
      r.d3dDesc = resource->GetDesc();
    }

    buffers_.push_back(std::move(r));
    compiled_ = false;
    return BufferHandle{ static_cast<uint32_t>(buffers_.size() - 1) };
  }

  void RenderGraph::setFinalState(TextureHandle h, D3D12_RESOURCE_STATES state)
  {
    if (!valid(h) || h.id >= textures_.size()) return;
    textures_[h.id].finalState = state;
  }

  void RenderGraph::setFinalState(BufferHandle h, D3D12_RESOURCE_STATES state)
  {
    if (!valid(h) || h.id >= buffers_.size()) return;
    buffers_[h.id].finalState = state;
  }

  // -----------------------------
  // Builder: unique per-resource use
  // -----------------------------
  void RenderGraph::addOrUpdateUse_(uint32_t passIndex, const ResourceUse& use)
  {
    auto& u = passes_[passIndex].uses;

    // Find same resource in this pass.
    for (auto& existing : u) {
      if (existing.kind == use.kind && existing.id == use.id) {
        // Merge rules:
        // - If any usage is Write => Write.
        // - For Read-only usage, OR the required states (helps PS|NonPS combos).
        // - If it becomes Write, requiredState becomes the latest specified write state.
        if (use.access == Access::Write) {
          existing.access = Access::Write;
          existing.requiredState = use.requiredState;
        } else {
          if (existing.access == Access::Read) {
            existing.requiredState = (existing.requiredState | use.requiredState);
          }
          // If existing is Write, keep it as-is.
        }
        return;
      }
    }

    u.push_back(use);
  }

  void RenderGraph::PassBuilder::read(TextureHandle h, D3D12_RESOURCE_STATES requiredState)
  {
    rg_->addOrUpdateUse_(passIndex_, ResourceUse{ ResourceUse::Kind::Texture, h.id, Access::Read, requiredState });
  }

  void RenderGraph::PassBuilder::write(TextureHandle h, D3D12_RESOURCE_STATES requiredState)
  {
    rg_->addOrUpdateUse_(passIndex_, ResourceUse{ ResourceUse::Kind::Texture, h.id, Access::Write, requiredState });
  }

  void RenderGraph::PassBuilder::read(BufferHandle h, D3D12_RESOURCE_STATES requiredState)
  {
    rg_->addOrUpdateUse_(passIndex_, ResourceUse{ ResourceUse::Kind::Buffer, h.id, Access::Read, requiredState });
  }

  void RenderGraph::PassBuilder::write(BufferHandle h, D3D12_RESOURCE_STATES requiredState)
  {
    rg_->addOrUpdateUse_(passIndex_, ResourceUse{ ResourceUse::Kind::Buffer, h.id, Access::Write, requiredState });
  }

  // -----------------------------
  // Compile
  // -----------------------------
  bool RenderGraph::compile(ID3D12Device* device, std::string* outError)
  {
    if (!device) {
      if (outError) *outError = "RenderGraph::compile: device is null.";
      return false;
    }

    // Validate handles referenced by passes.
    for (uint32_t p = 0; p < passes_.size(); ++p) {
      for (const auto& use : passes_[p].uses) {
        if (use.kind == ResourceUse::Kind::Texture) {
          if (use.id >= textures_.size()) {
            if (outError) *outError = "RenderGraph::compile: pass uses invalid TextureHandle.";
            return false;
          }
        } else {
          if (use.id >= buffers_.size()) {
            if (outError) *outError = "RenderGraph::compile: pass uses invalid BufferHandle.";
            return false;
          }
        }
      }
    }

    if (!createOwnedResources_(device, outError)) {
      return false;
    }

    if (!buildExecutionOrder_(outError)) {
      return false;
    }

    resetStateTracking_();
    compiled_ = true;
    return true;
  }

  bool RenderGraph::createOwnedResources_(ID3D12Device* device, std::string* outError)
  {
    // (Re)create all owned resources.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    for (auto& t : textures_) {
      if (t.imported) {
        if (!t.external) {
          if (outError) *outError = "RenderGraph::compile: imported texture has null resource pointer.";
          return false;
        }
        t.d3dDesc = t.external->GetDesc();
        continue;
      }

      t.owned.Reset();

      const D3D12_RESOURCE_DESC d = t.createDesc.desc;
      const D3D12_CLEAR_VALUE* cv = t.createDesc.clearValue ? &(*t.createDesc.clearValue) : nullptr;

      HRESULT hr = device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &d,
        t.initialState,
        cv,
        IID_PPV_ARGS(t.owned.ReleaseAndGetAddressOf()));

      if (FAILED(hr) || !t.owned) {
        if (outError) {
          std::ostringstream oss;
          oss << "RenderGraph::compile: failed to create texture '" << t.name
              << "' (HRESULT=0x" << std::hex << (uint32_t)hr << ").";
          *outError = oss.str();
        }
        return false;
      }

      t.d3dDesc = d;
      t.owned->SetName(toWideAscii_(t.name).c_str());
    }

    for (auto& b : buffers_) {
      if (b.imported) {
        if (!b.external) {
          if (outError) *outError = "RenderGraph::compile: imported buffer has null resource pointer.";
          return false;
        }
        b.d3dDesc = b.external->GetDesc();
        continue;
      }

      b.owned.Reset();

      const D3D12_RESOURCE_DESC d = b.createDesc.desc;

      HRESULT hr = device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &d,
        b.initialState,
        nullptr,
        IID_PPV_ARGS(b.owned.ReleaseAndGetAddressOf()));

      if (FAILED(hr) || !b.owned) {
        if (outError) {
          std::ostringstream oss;
          oss << "RenderGraph::compile: failed to create buffer '" << b.name
              << "' (HRESULT=0x" << std::hex << (uint32_t)hr << ").";
          *outError = oss.str();
        }
        return false;
      }

      b.d3dDesc = d;
      b.owned->SetName(toWideAscii_(b.name).c_str());
    }

    return true;
  }

  bool RenderGraph::buildExecutionOrder_(std::string* outError)
  {
    executionOrder_.clear();

    const uint32_t passCount = static_cast<uint32_t>(passes_.size());
    if (passCount == 0) {
      // Trivial graph.
      return true;
    }

    // Build a dependency graph (edges A -> B means B depends on A).
    std::vector<std::vector<uint32_t>> edges(passCount);
    std::vector<uint32_t> indegree(passCount, 0);

    auto addEdge = [&](uint32_t from, uint32_t to) {
      if (from == to) return;
      auto& e = edges[from];
      if (std::find(e.begin(), e.end(), to) == e.end()) {
        e.push_back(to);
        indegree[to]++;
      }
    };

    // Track last writer + last readers per resource (textures/buffers separately).
    std::vector<int32_t> lastWriterTex(textures_.size(), -1);
    std::vector<std::vector<uint32_t>> lastReadersTex(textures_.size());

    std::vector<int32_t> lastWriterBuf(buffers_.size(), -1);
    std::vector<std::vector<uint32_t>> lastReadersBuf(buffers_.size());

    for (uint32_t p = 0; p < passCount; ++p) {
      for (const auto& use : passes_[p].uses) {
        if (use.kind == ResourceUse::Kind::Texture) {
          const uint32_t rid = use.id;

          if (use.access == Access::Read) {
            if (lastWriterTex[rid] >= 0) addEdge(static_cast<uint32_t>(lastWriterTex[rid]), p);
            lastReadersTex[rid].push_back(p);
          } else {
            if (lastWriterTex[rid] >= 0) addEdge(static_cast<uint32_t>(lastWriterTex[rid]), p);
            for (uint32_t r : lastReadersTex[rid]) addEdge(r, p);
            lastReadersTex[rid].clear();
            lastWriterTex[rid] = static_cast<int32_t>(p);
          }
        } else {
          const uint32_t rid = use.id;

          if (use.access == Access::Read) {
            if (lastWriterBuf[rid] >= 0) addEdge(static_cast<uint32_t>(lastWriterBuf[rid]), p);
            lastReadersBuf[rid].push_back(p);
          } else {
            if (lastWriterBuf[rid] >= 0) addEdge(static_cast<uint32_t>(lastWriterBuf[rid]), p);
            for (uint32_t r : lastReadersBuf[rid]) addEdge(r, p);
            lastReadersBuf[rid].clear();
            lastWriterBuf[rid] = static_cast<int32_t>(p);
          }
        }
      }
    }

    // Topological sort (Kahn). Keep a stable-ish order by pushing nodes in index order.
    std::deque<uint32_t> q;
    for (uint32_t i = 0; i < passCount; ++i) {
      if (indegree[i] == 0) q.push_back(i);
    }

    while (!q.empty()) {
      uint32_t n = q.front();
      q.pop_front();
      executionOrder_.push_back(n);

      for (uint32_t to : edges[n]) {
        if (--indegree[to] == 0) q.push_back(to);
      }
    }

    if (executionOrder_.size() != passCount) {
      if (outError) {
        *outError =
          "RenderGraph::compile: dependency cycle detected (a pass writes/reads in a cyclic way).";
      }
      return false;
    }

    return true;
  }

  void RenderGraph::resetStateTracking_()
  {
    for (auto& t : textures_) {
      t.currentState = t.initialState;
      t.lastAccess = Access::Read;
      t.lastWasUav = false;
    }
    for (auto& b : buffers_) {
      b.currentState = b.initialState;
      b.lastAccess = Access::Read;
      b.lastWasUav = false;
    }
  }

  // -----------------------------
  // Execute
  // -----------------------------
  void RenderGraph::execute(ID3D12GraphicsCommandList* cmd)
  {
    if (!compiled_) return;
    if (!cmd) return;

    RenderGraphResources res(*this);

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(64);

    auto transitionIfNeededTexture = [&](TextureRes& r,
                                         D3D12_RESOURCE_STATES required,
                                         Access access,
                                         bool requiredIsUav)
    {
      ID3D12Resource* resource = r.imported ? r.external : r.owned.Get();
      if (!resource) return;

      // Transition rule:
      // - Writes want exact state match.
      // - Reads can skip if current is a superset of required (common for combined read states).
      bool needTransition = false;
      if (access == Access::Write) {
        needTransition = (r.currentState != required);
      } else {
        needTransition = ((r.currentState & required) != required);
      }

      // UAV barrier rule:
      // If we stay in UAV state and either side writes, insert UAV barrier.
      const bool stayingUav = ((r.currentState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) &&
                              ((required      & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) &&
                              !needTransition;

      if (stayingUav && (r.lastAccess == Access::Write || access == Access::Write || r.lastWasUav || requiredIsUav)) {
        barriers.push_back(makeUavBarrier_(resource));
      }

      if (needTransition) {
        barriers.push_back(makeTransition_(resource, r.currentState, required));
        r.currentState = required;
      }

      r.lastAccess = access;
      r.lastWasUav = requiredIsUav;
    };

    auto transitionIfNeededBuffer = [&](BufferRes& r,
                                        D3D12_RESOURCE_STATES required,
                                        Access access,
                                        bool requiredIsUav)
    {
      ID3D12Resource* resource = r.imported ? r.external : r.owned.Get();
      if (!resource) return;

      bool needTransition = false;
      if (access == Access::Write) {
        needTransition = (r.currentState != required);
      } else {
        needTransition = ((r.currentState & required) != required);
      }

      const bool stayingUav = ((r.currentState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) &&
                              ((required      & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) &&
                              !needTransition;

      if (stayingUav && (r.lastAccess == Access::Write || access == Access::Write || r.lastWasUav || requiredIsUav)) {
        barriers.push_back(makeUavBarrier_(resource));
      }

      if (needTransition) {
        barriers.push_back(makeTransition_(resource, r.currentState, required));
        r.currentState = required;
      }

      r.lastAccess = access;
      r.lastWasUav = requiredIsUav;
    };

    // Run passes in computed order.
    for (uint32_t passIdx : executionOrder_) {
      auto& pass = passes_[passIdx];

      barriers.clear();

      // Emit transitions/UAV barriers to satisfy this pass.
      for (const auto& use : pass.uses) {
        const bool isUav = use.isUav();

        if (use.kind == ResourceUse::Kind::Texture) {
          transitionIfNeededTexture(textures_[use.id], use.requiredState, use.access, isUav);
        } else {
          transitionIfNeededBuffer(buffers_[use.id], use.requiredState, use.access, isUav);
        }
      }

      if (!barriers.empty()) {
        cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      }

      // Execute pass callback
      if (pass.exec) {
        pass.exec(cmd, res);
      }
    }

    // Post-graph final transitions (useful for back buffer -> PRESENT).
    barriers.clear();

    for (auto& t : textures_) {
      if (!t.finalState) continue;
      if (t.currentState == *t.finalState) continue;
      ID3D12Resource* r = t.imported ? t.external : t.owned.Get();
      if (!r) continue;
      barriers.push_back(makeTransition_(r, t.currentState, *t.finalState));
      t.currentState = *t.finalState;
    }

    for (auto& b : buffers_) {
      if (!b.finalState) continue;
      if (b.currentState == *b.finalState) continue;
      ID3D12Resource* r = b.imported ? b.external : b.owned.Get();
      if (!r) continue;
      barriers.push_back(makeTransition_(r, b.currentState, *b.finalState));
      b.currentState = *b.finalState;
    }

    if (!barriers.empty()) {
      cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
  }

  // -----------------------------
  // Internal access
  // -----------------------------
  ID3D12Resource* RenderGraph::getTexture_(TextureHandle h) const
  {
    if (!valid(h) || h.id >= textures_.size()) return nullptr;
    const auto& r = textures_[h.id];
    return r.imported ? r.external : r.owned.Get();
  }

  ID3D12Resource* RenderGraph::getBuffer_(BufferHandle h) const
  {
    if (!valid(h) || h.id >= buffers_.size()) return nullptr;
    const auto& r = buffers_[h.id];
    return r.imported ? r.external : r.owned.Get();
  }

} // namespace cg::rendergraph
