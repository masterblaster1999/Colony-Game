#pragma once
#include "TileTypes.h"
#include "RendererHooks.h"
#include "JobSystem.h"
#include "InstallPaths.h"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <optional>
#include <filesystem>

struct TerrainStreamingConfig {
    int   radiusTiles = 3;            // hot radius in tiles in X/Y from camera
    float tileWorldSize = 256.0f;     // meters per tile (sample spacing assumed 1 m here)
    int   maxHeightTiles = 512;       // residency limits
    int   maxTextureTiles = 1024;
};

class TerrainStreamer {
public:
    explicit TerrainStreamer(JobSystem& jobs, ITerrainRenderer& renderer);

    void Configure(const TerrainStreamingConfig& cfg);

    // Call each frame with camera position in world units (x,z on plane).
    void Update(float camX, float camZ);

    // Finish CPU→GPU uploads on main thread.
    void PumpUploads();

    // For testing & HUD
    int ResidentHeightTiles() const { return (int)m_heightTiles.size(); }
    int ResidentTextureTiles() const { return (int)m_texTiles.size(); }

private:
    struct TileState {
        enum class State : uint8_t { Loading, Resident } state = State::Loading;
        uint64_t lastTouchFrame = 0;
    };

    struct PendingTex {
        TileCoord key;
        TileKind kind;
        std::unique_ptr<class DdsOwned> owned; // keeps ScratchImage alive
    };

    struct PendingHeight {
        TileCoord key;
        std::unique_ptr<HeightTileCPU> ht;
    };

    void RequestNeighborhood(const TileCoord& center);
    void RequestSingle(const TileCoord& key);

    void EnqueueHeightLoad(const TileCoord& key, int priority);
    void EnqueueTextureLoad(const TileCoord& key, TileKind kind, int priority);

    void EvictIfOverBudget();

    // File resolvers
    std::filesystem::path HeightPath(const TileCoord& k) const;
    std::filesystem::path TexPath(const TileCoord& k, TileKind kind) const;

    // Completion queues
    void PushReadyTex(PendingTex&& p);
    void PushReadyHeight(PendingHeight&& p);

private:
    JobSystem& m_jobs;
    ITerrainRenderer& m_renderer;
    TerrainStreamingConfig m_cfg{};
    TerrainStreamingDirs m_paths = GetTerrainDirs();

    // Frame counter for simple LRU touch
    uint64_t m_frame = 0;

    std::unordered_map<TileCoord, TileState, TileCoordHasher> m_heightTiles;
    std::unordered_map<TileCoord, TileState, TileCoordHasher> m_texTilesA; // albedo
    std::unordered_map<TileCoord, TileState, TileCoordHasher> m_texTilesN; // normal

    mutable std::mutex m_readyMx;
    std::queue<PendingTex>    m_readyTex;
    std::queue<PendingHeight> m_readyHt;
};
