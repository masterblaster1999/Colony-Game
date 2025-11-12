#include "TerrainStreaming.h"
#include "TileIO.h"
#include <cmath>
#include <algorithm>

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
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            TileCoord k{ center.x + dx, center.y + dy, 0 };
            RequestSingle(k);
        }
    }
}

void TerrainStreamer::RequestSingle(const TileCoord& key)
{
    const int dist = std::abs(key.x) + std::abs(key.y); // rough priority

    // Height
    auto itH = m_heightTiles.find(key);
    if (itH == m_heightTiles.end()) {
        m_heightTiles.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueHeightLoad(key, dist);
    } else {
        itH->second.lastTouchFrame = m_frame;
    }

    // Albedo
    auto itA = m_texTilesA.find(key);
    if (itA == m_texTilesA.end()) {
        m_texTilesA.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueTextureLoad(key, TileKind::Albedo, dist);
    } else {
        itA->second.lastTouchFrame = m_frame;
    }

    // Normal
    auto itN = m_texTilesN.find(key);
    if (itN == m_texTilesN.end()) {
        m_texTilesN.emplace(key, TileState{TileState::State::Loading, m_frame});
        EnqueueTextureLoad(key, TileKind::Normal, dist);
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
    // Drain completions; create/update GPU resources via renderer hooks.
    {
        std::lock_guard<std::mutex> lk(m_readyMx);
        while (!m_readyHt.empty()) {
            PendingHeight p = std::move(m_readyHt.front()); m_readyHt.pop();
            if (p.ht) {
                m_renderer.UploadHeightTile(p.key, *p.ht);
                auto it = m_heightTiles.find(p.key);
                if (it != m_heightTiles.end()) it->second.state = TileState::State::Resident;
            }
        }
        while (!m_readyTex.empty()) {
            PendingTex p = std::move(m_readyTex.front()); m_readyTex.pop();
            if (p.owned && p.owned->img) {
                TextureTileCPU cpu{ p.owned->img.get() };
                m_renderer.UploadTextureTile(p.key, p.kind, cpu);
                if (p.kind == TileKind::Albedo) {
                    auto it = m_texTilesA.find(p.key);
                    if (it != m_texTilesA.end()) it->second.state = TileState::State::Resident;
                } else {
                    auto it = m_texTilesN.find(p.key);
                    if (it != m_texTilesN.end()) it->second.state = TileState::State::Resident;
                }
            }
        }
    }
}

void TerrainStreamer::EvictIfOverBudget()
{
    const auto evictLRU = [&](auto& map, int maxCount, TileKind kind){
        if ((int)map.size() <= maxCount) return;
        // find oldest by lastTouchFrame
        using PairT = typename std::decay_t<decltype(map)>::value_type;
        std::vector<PairT*> items; items.reserve(map.size());
        for (auto& kv : map) items.push_back(&kv);
        std::sort(items.begin(), items.end(), [](const PairT* a, const PairT* b){
            return a->second.lastTouchFrame < b->second.lastTouchFrame;
        });
        const int overflow = (int)map.size() - maxCount;
        for (int i=0; i<overflow; ++i) {
            const TileCoord key = items[i]->first;
            m_renderer.EvictTile(key, kind);
            map.erase(key);
        }
    };

    evictLRU(m_heightTiles, m_cfg.maxHeightTiles, TileKind::Height);
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
