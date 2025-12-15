#pragma once
//
// src/renderer/RenderGraph.h
//
// A small, practical D3D12 RenderGraph for Colony-Game.
//
// Goals:
//  - Declare resources (create or import).
//  - Declare passes with read/write usage + required resource states.
//  - Compile: build dependencies (topological order) + create owned resources.
//  - Execute: automatically emit batched D3D12 barriers + run pass callbacks.
//
// Notes:
//  - This does NOT manage descriptor heaps (RTV/DSV/SRV/UAV) yet.
//    Passes receive ID3D12Resource* and can use your existing descriptor system.
//  - For imported resources (swapchain back buffer), set a final state (e.g. PRESENT)
//    so the next frame starts from a known state.
//  - Designed for Windows-only D3D12.
//
// References:
//  - D3D12 ResourceBarrier docs & guidance: ID3D12GraphicsCommandList::ResourceBarrier,
//    D3D12_RESOURCE_BARRIER, transitions, UAV barriers.
//

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <optional>

namespace cg::rendergraph {

  // -----------------------------
  // Strong-ish handles (tiny API)
  // -----------------------------
  struct TextureHandle {
    uint32_t id = std::numeric_limits<uint32_t>::max();
  };
  struct BufferHandle {
    uint32_t id = std::numeric_limits<uint32_t>::max();
  };

  [[nodiscard]] inline constexpr bool valid(TextureHandle h) noexcept {
    return h.id != std::numeric_limits<uint32_t>::max();
  }
  [[nodiscard]] inline constexpr bool valid(BufferHandle h) noexcept {
    return h.id != std::numeric_limits<uint32_t>::max();
  }

  // -----------------------------
  // Resource descriptions
  // -----------------------------
  struct TextureDesc {
    D3D12_RESOURCE_DESC desc{};
    std::optional<D3D12_CLEAR_VALUE> clearValue;

    // Convenience: Plain 2D texture.
    [[nodiscard]] static TextureDesc Tex2D(
      UINT width,
      UINT height,
      DXGI_FORMAT format,
      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
      UINT16 mipLevels = 1,
      UINT16 arraySize = 1,
      UINT sampleCount = 1,
      UINT sampleQuality = 0);

    // Convenience: Render-target 2D texture (+ optional clear color).
    [[nodiscard]] static TextureDesc RenderTarget2D(
      UINT width,
      UINT height,
      DXGI_FORMAT format,
      const float* clearColorRGBA4 = nullptr,
      UINT16 mipLevels = 1,
      UINT16 arraySize = 1,
      UINT sampleCount = 1,
      UINT sampleQuality = 0);

    // Convenience: Depth-stencil 2D texture (+ clear depth/stencil).
    [[nodiscard]] static TextureDesc DepthStencil2D(
      UINT width,
      UINT height,
      DXGI_FORMAT format,
      float clearDepth = 1.0f,
      uint8_t clearStencil = 0,
      UINT sampleCount = 1,
      UINT sampleQuality = 0);
  };

  struct BufferDesc {
    D3D12_RESOURCE_DESC desc{};

    [[nodiscard]] static BufferDesc Buffer(
      uint64_t byteSize,
      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
      uint64_t alignment = 0);
  };

  // -----------------------------
  // RenderGraph resource view
  // -----------------------------
  class RenderGraph;

  class RenderGraphResources {
  public:
    [[nodiscard]] ID3D12Resource* texture(TextureHandle h) const;
    [[nodiscard]] ID3D12Resource* buffer(BufferHandle h) const;

    [[nodiscard]] const D3D12_RESOURCE_DESC& textureDesc(TextureHandle h) const;
    [[nodiscard]] const D3D12_RESOURCE_DESC& bufferDesc(BufferHandle h) const;

  private:
    friend class RenderGraph;
    explicit RenderGraphResources(const RenderGraph& rg) : rg_(&rg) {}
    const RenderGraph* rg_ = nullptr;
  };

  // -----------------------------
  // RenderGraph
  // -----------------------------
  class RenderGraph {
  public:
    enum class Access : uint8_t { Read, Write };

    using PassExecFn = std::function<void(ID3D12GraphicsCommandList*, const RenderGraphResources&)>;

    class PassBuilder {
    public:
      // Texture usage
      void read(TextureHandle h, D3D12_RESOURCE_STATES requiredState);
      void write(TextureHandle h, D3D12_RESOURCE_STATES requiredState);

      // Buffer usage
      void read(BufferHandle h, D3D12_RESOURCE_STATES requiredState);
      void write(BufferHandle h, D3D12_RESOURCE_STATES requiredState);

    private:
      friend class RenderGraph;
      PassBuilder(RenderGraph& rg, uint32_t passIndex) : rg_(&rg), passIndex_(passIndex) {}
      RenderGraph* rg_ = nullptr;
      uint32_t passIndex_ = 0;
    };

    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& operator=(RenderGraph&&) noexcept = default;

    // Clear everything (passes + resources).
    void clear();

    // -----------------------------
    // Resources
    // -----------------------------
    TextureHandle createTexture(std::string name, const TextureDesc& desc, D3D12_RESOURCE_STATES initialState);
    BufferHandle  createBuffer (std::string name, const BufferDesc&  desc, D3D12_RESOURCE_STATES initialState);

    // Imported resources are not owned; graph only tracks state + uses.
    TextureHandle importTexture(std::string name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState);
    BufferHandle  importBuffer (std::string name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState);

    // Optional: ensure specific resource state after the graph executes.
    // Typical use: back buffer -> PRESENT.
    void setFinalState(TextureHandle h, D3D12_RESOURCE_STATES state);
    void setFinalState(BufferHandle  h, D3D12_RESOURCE_STATES state);

    // -----------------------------
    // Passes
    // -----------------------------
    template <class SetupFn, class ExecFn>
    uint32_t addPass(std::string name, SetupFn&& setup, ExecFn&& exec)
    {
      const uint32_t idx = static_cast<uint32_t>(passes_.size());
      passes_.push_back(PassNode{});
      passes_.back().name = std::move(name);
      passes_.back().exec = PassExecFn(std::forward<ExecFn>(exec));

      PassBuilder b(*this, idx);
      setup(b);
      compiled_ = false;
      return idx;
    }

    // -----------------------------
    // Compile / Execute
    // -----------------------------
    // Creates owned resources (if any) and computes a safe execution order.
    bool compile(ID3D12Device* device, std::string* outError = nullptr);

    // Emits barriers (batched) and runs passes in compiled order.
    // Requires: compile() success.
    void execute(ID3D12GraphicsCommandList* cmd);

    [[nodiscard]] bool isCompiled() const noexcept { return compiled_; }

  private:
    struct ResourceUse {
      enum class Kind : uint8_t { Texture, Buffer };

      Kind kind{};
      uint32_t id = 0;
      Access access = Access::Read;
      D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_COMMON;

      [[nodiscard]] bool isUav() const noexcept {
        return (requiredState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0;
      }
    };

    struct TextureRes {
      std::string name;
      bool imported = false;

      // For created resources:
      TextureDesc createDesc{};
      Microsoft::WRL::ComPtr<ID3D12Resource> owned{};

      // For imported resources:
      ID3D12Resource* external = nullptr;

      D3D12_RESOURCE_DESC d3dDesc{};
      D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
      D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
      std::optional<D3D12_RESOURCE_STATES> finalState;

      Access lastAccess = Access::Read;
      bool lastWasUav = false;
    };

    struct BufferRes {
      std::string name;
      bool imported = false;

      BufferDesc createDesc{};
      Microsoft::WRL::ComPtr<ID3D12Resource> owned{};

      ID3D12Resource* external = nullptr;

      D3D12_RESOURCE_DESC d3dDesc{};
      D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
      D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
      std::optional<D3D12_RESOURCE_STATES> finalState;

      Access lastAccess = Access::Read;
      bool lastWasUav = false;
    };

    struct PassNode {
      std::string name;
      std::vector<ResourceUse> uses;
      PassExecFn exec;
    };

    // Builder helper: add or update a use in a pass (ensures per-resource uniqueness per pass).
    void addOrUpdateUse_(uint32_t passIndex, const ResourceUse& use);

    // Compile helpers
    bool createOwnedResources_(ID3D12Device* device, std::string* outError);
    bool buildExecutionOrder_(std::string* outError);

    void resetStateTracking_();

    // Access helpers
    [[nodiscard]] ID3D12Resource* getTexture_(TextureHandle h) const;
    [[nodiscard]] ID3D12Resource* getBuffer_ (BufferHandle h) const;

  private:
    friend class RenderGraphResources;

    std::vector<TextureRes> textures_;
    std::vector<BufferRes>  buffers_;
    std::vector<PassNode>   passes_;

    std::vector<uint32_t> executionOrder_;
    bool compiled_ = false;
  };

} // namespace cg::rendergraph
