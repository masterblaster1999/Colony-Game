#include "TerrainStreaming.h"
#include "TileIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

TerrainStreamer::TerrainStreamer(JobSystem& jobs, ITerrainRenderer& renderer)
: m_jobs(jobs), m_renderer(renderer) {}

void TerrainStreamer::Configure(const TerrainStreamingConfig& cfg) { m_cfg = cfg; }

static inline TileCoord WorldToTile(float x, float z, float tileWorldSize)
{
    const int tx = (int)std::floor(x / tileWorldSize);
    const int ty = (int)std::floor(z / tileWorldSize);
    return TileCoord{ tx, ty, 0 };
}

void TerrainStreamer::Update(float camX, float camZ)
{
    ++m_frame;
    const TileCoord center = WorldToTile(camX, camZ, m_cfg.tileWorldSize);
    RequestNeighborhood(center);
    EvictIfOverBudget();
}

void TerrainStreamer::RequestNeighborhood(const TileCoord& center)
{
    const int r = std::max(1, m_cfg.radiusTiles);

    // JobSystem runs *lower* priority values earlier.
    // We encode (distance, asset type) into a single integer so the scheduler naturally
    // pulls the closest tiles first, and within the same distance loads Height first
    // (then Albedo, then Normal).
    constexpr int kPriorityStride = 4;
    constexpr int kHeightBias = 0;
    constexpr int kAlbedoBias = 1;
    constexpr int kNormalBias = 2;

    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {

            const int dist = std::abs(dx) + std::abs(dy); // distance from camera-center tile
            const TileCoord key{ center.x + dx, center.y + dy, 0 };

            // Height
            {
                auto itH = m_heightTiles.find(key);
                if (itH == m_heightTiles.end()) {
                    m_heightTiles.emplace(key, TileState{TileState::State::Loading, m_frame});
                    EnqueueHeightLoad(key, dist * kPriorityStride + kHeightBias);
                } else {
                    itH->second.lastTouchFrame = m_frame;
                }
            }

            // Albedo
            {
                auto itA = m_texTilesA.find(key);
                if (itA == m_texTilesA.end()) {
                    m_texTilesA.emplace(key, TileState{TileState::State::Loading, m_frame});
                    EnqueueTextureLoad(key, TileKind::Albedo, dist * kPriorityStride + kAlbedoBias);
                } else {
                    itA->second.lastTouchFrame = m_frame;
                }
            }

            // Normal
            {
                auto itN = m_texTilesN.find(key);
                if (itN == m_texTilesN.end()) {
                    m_texTilesN.emplace(key, TileState{TileState::State::Loading, m_frame});
                    EnqueueTextureLoad(key, TileKind::Normal, dist * kPriorityStride + kNormalBias);
                } else {
                    itN->second.lastTouchFrame = m_frame;
                }
            }
        }
    }
}

// Kept for compatibility with the existing class layout (declared in the header).
// NOTE: This lacks camera-center context, so we treat it as "very important" (dist=0).
void TerrainStreamer::RequestSingle(const TileCoord& key)
{
    constexpr int kPriorityStride = 4;
    constexpr int kHeightBias = 0;
    constexpr int kAlbedoBias = 1;
    constexpr int kNormalBias = 2;

    const int dist = 0;

    // Height
    auto itH = m_heightTiles.find(key);
    if (itH == m_heightTiles.end()) {
        m_heightTiles.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueHeightLoad(key, dist * kPriorityStride + kHeightBias);
    } else {
        itH->second.lastTouchFrame = m_frame;
    }

    // Albedo
    auto itA = m_texTilesA.find(key);
    if (itA == m_texTilesA.end()) {
        m_texTilesA.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueTextureLoad(key, TileKind::Albedo, dist * kPriorityStride + kAlbedoBias);
    } else {
        itA->second.lastTouchFrame = m_frame;
    }

    // Normal
    auto itN = m_texTilesN.find(key);
    if (itN == m_texTilesN.end()) {
        m_texTilesN.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueTextureLoad(key, TileKind::Normal, dist * kPriorityStride + kNormalBias);
    } else {
        itN->second.lastTouchFrame = m_frame;
    }
}

void TerrainStreamer::EnqueueHeightLoad(const TileCoord& key, int priority)
{
    const auto path = HeightPath(key);
    m_jobs.Submit([this, path, key]{
        auto ht = LoadHeightTileR16(path);
        if (ht) { this->PushReadyHeight(PendingHeight{ key, std::move(ht) }); }
    }, priority);
}

void TerrainStreamer::EnqueueTextureLoad(const TileCoord& key, TileKind kind, int priority)
{
    const auto path = TexPath(key, kind);
    m_jobs.Submit([this, path, key, kind]{
        auto dds = LoadDdsOwned(path);
        if (dds) { this->PushReadyTex(PendingTex{ key, kind, std::move(dds) }); }
    }, priority);
}

void TerrainStreamer::PushReadyHeight(PendingHeight&& p)
{
    std::lock_guard<std::mutex> lk(m_readyMx);
    m_readyHt.emplace(std::move(p));
}

void TerrainStreamer::PushReadyTex(PendingTex&& p)
{
    std::lock_guard<std::mutex> lk(m_readyMx);
    m_readyTex.emplace(std::move(p));
}

void TerrainStreamer::PumpUploads()
{
    // IMPORTANT: do not hold m_readyMx while calling into the renderer.
    // Uploading creates GPU resources and can be slow; holding the lock here stalls worker threads
    // that are trying to push completions.

    std::queue<PendingHeight> readyHt;
    std::queue<PendingTex> readyTex;

    {
        std::lock_guard<std::mutex> lk(m_readyMx);
        std::swap(readyHt, m_readyHt);
        std::swap(readyTex, m_readyTex);
    }

    // Heights
    while (!readyHt.empty()) {
        PendingHeight p = std::move(readyHt.front());
        readyHt.pop();

        if (!p.ht)
            continue;

        // If we evicted this tile while it was loading, drop the completion (avoid wasted upload).
        auto it = m_heightTiles.find(p.key);
        if (it == m_heightTiles.end())
            continue;

        m_renderer.UploadHeightTile(p.key, *p.ht);
        it->second.state = TileState::State::Resident;
    }

    // Textures
    while (!readyTex.empty()) {
        PendingTex p = std::move(readyTex.front());
        readyTex.pop();

        if (!p.owned || !p.owned->img)
            continue;

        auto& map = (p.kind == TileKind::Albedo) ? m_texTilesA : m_texTilesN;

        // If we evicted this tile while it was loading, drop the completion (avoid wasted upload).
        auto it = map.find(p.key);
        if (it == map.end())
            continue;

        TextureTileCPU cpu{ p.owned->img.get() };
        m_renderer.UploadTextureTile(p.key, p.kind, cpu);
        it->second.state = TileState::State::Resident;
    }
}

void TerrainStreamer::EvictIfOverBudget()
{
    const auto evictLRU = [&](auto& map, int maxCount, TileKind kind){
        if (maxCount < 0) return; // "no budget" guard
        const size_t maxCountSz = static_cast<size_t>(maxCount);
        if (map.size() <= maxCountSz) return;

        const size_t overflow = map.size() - maxCountSz;
        if (overflow == 0) return;

        using PairT = typename std::decay_t<decltype(map)>::value_type;

        std::vector<PairT*> items;
        items.reserve(map.size());
        for (auto& kv : map) items.push_back(&kv);

        const auto olderFirst = [](const PairT* a, const PairT* b){
            return a->second.lastTouchFrame < b->second.lastTouchFrame;
        };

        // Partition so the 'overflow' oldest items are in the first segment.
        // (Avoid sorting the entire vector: smoother frames when budgets are large.)
        if (overflow < items.size()) {
            std::nth_element(items.begin(), items.begin() + overflow, items.end(), olderFirst);
        }

        for (size_t i = 0; i < overflow && i < items.size(); ++i) {
            const TileCoord key = items[i]->first;

            // Only evict from the renderer if we ever made it resident on GPU.
            if (items[i]->second.state == TileState::State::Resident) {
                m_renderer.EvictTile(key, kind);
            }
            map.erase(key);
        }
    };

    evictLRU(m_heightTiles, m_cfg.maxHeightTiles,  TileKind::Height);
    evictLRU(m_texTilesA,   m_cfg.maxTextureTiles, TileKind::Albedo);
    evictLRU(m_texTilesN,   m_cfg.maxTextureTiles, TileKind::Normal);
}

std::filesystem::path TerrainStreamer::HeightPath(const TileCoord& k) const
{
    // .../Content/Streaming/Terrain/Height/X{tileX}_Y{tileY}.r16
    wchar_t name[64];
    swprintf_s(name, L"X%d_Y%d.r16", k.x, k.y);
    return m_paths.height / name;
}

std::filesystem::path TerrainStreamer::TexPath(const TileCoord& k, TileKind kind) const
{
    wchar_t name[64];
    swprintf_s(name, L"X%d_Y%d.dds", k.x, k.y);
    const auto& dir = (kind == TileKind::Albedo) ? m_paths.albedo : m_paths.normal;
    return dir / name;
}
