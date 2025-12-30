#include "game/PrototypeGame_Impl.h"
#include "game/Role.hpp"
#include <cstdint>

#include "game/proto/ProtoWorld_SaveFormat.h"

#include "platform/win/WinCommon.h"


#include "platform/win/PathUtilWin.h"
#include "util/PathUtf8.h"

#include "game/save/Base64.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace colony::game {

namespace {

using json = nlohmann::json;

[[nodiscard]] std::int64_t UnixSecondsUtcNow() noexcept
{
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    return static_cast<std::int64_t>(secs.time_since_epoch().count());
}

} // namespace

// ----------------------------------------------------------------------------
// AsyncSaveManager (prototype)
// ----------------------------------------------------------------------------

struct AsyncSaveManager {
    enum class Kind : std::uint8_t { Manual = 0, Autosave };

    struct Cell {
        std::uint8_t built = 0;
        std::uint8_t planned = 0;
        std::uint8_t planPriority = 0;
        std::uint8_t builtFromPlan = 0; // v4+ (0/1)
        float workRemaining = 0.0f;
        float farmGrowth = 0.0f;
        int looseWood = 0;
    };
    struct Colonist {
        int id = 0;
        float x = 0.5f;
        float y = 0.5f;

        // v3+ hunger
        float personalFood = 0.0f;

        // v7+: roles + drafted state
        bool drafted = false;
        RoleId role = RoleId::Worker;
        std::uint16_t roleLevel = 1;
        std::uint32_t roleXp = 0;

        // v9+: work priorities
        std::uint8_t workPrioBuild = 2;
        std::uint8_t workPrioFarm  = 2;
        std::uint8_t workPrioHaul  = 2;
    };


    struct Snapshot {
        int w = 0;
        int h = 0;

        int wood = 0;
        float food = 0.0f;

        double buildWorkPerSecond = 1.0;
        double colonistWalkSpeed = 3.0;
        double farmGrowDurationSeconds = 40.0;
        double farmHarvestYieldFood = 10.0;
        double farmHarvestDurationSeconds = 1.0;

        // v6+ forestry tuning
        int treeChopYieldWood = 4;
        double treeSpreadAttemptsPerSecond = 2.5;
        double treeSpreadChancePerAttempt = 0.15;
        double foodPerColonistPerSecond = 0.05;

        // v3+ hunger/eating tuning
        double colonistMaxPersonalFood = 6.0;
        double colonistEatThresholdFood = 2.0;
        double colonistEatDurationSeconds = 1.5;

        // v8+ hauling tuning
        int haulCarryCapacity = 25;
        double haulPickupDurationSeconds = 0.25;
        double haulDropoffDurationSeconds = 0.25;

        // v11+ pathfinding tuning
        proto::PathAlgo pathAlgo = proto::PathAlgo::AStar;
        bool pathCacheEnabled = true;
        int pathCacheMaxEntries = 1024;
        bool navTerrainCostsEnabled = true;

        std::vector<Cell> cells;
        std::vector<Colonist> colonists;

        // Small summary data for save browser / UI.
        int plannedCount    = 0;
        int builtFloors     = 0;
        int builtWalls      = 0;
        int builtFarms      = 0;
        int builtStockpiles = 0;

        std::int64_t savedUnixSecondsUtc = 0;
        double playtimeSeconds = 0.0;
    };

    struct Completion {
        Kind kind = Kind::Manual;
        bool ok = false;
        bool showStatus = false;
        fs::path path;
        std::string message; // error text (on failure) or small note
    };

    AsyncSaveManager()
    {
        m_thread = std::thread([this] { workerMain(); });
    }

    ~AsyncSaveManager()
    {
        m_stop.store(true, std::memory_order_release);
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    AsyncSaveManager(const AsyncSaveManager&) = delete;
    AsyncSaveManager& operator=(const AsyncSaveManager&) = delete;

    void BumpAutosaveGeneration() noexcept
    {
        m_autosaveGeneration.fetch_add(1, std::memory_order_acq_rel);

        // Drop any queued autosaves; we don't want a world-before-load autosave to
        // overwrite the newest autosave after a load/reset.
        std::lock_guard<std::mutex> lock(m_mutex);
        std::deque<Task> kept;
        for (auto& t : m_queue)
        {
            if (t.kind != Kind::Autosave)
                kept.push_back(std::move(t));
        }
        m_queue.swap(kept);
    }

    void EnqueueManualSave(const proto::World& world,
                           const fs::path& path,
                           bool showStatus,
                           double playtimeSeconds)
    {
        try
        {
            Task t;
            t.kind = Kind::Manual;
            t.pathOrDir = path;
            t.pretty = true;
            t.showStatus = showStatus;
            t.snap = MakeSnapshot(world, playtimeSeconds);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push_back(std::move(t));
            }
            m_cv.notify_one();
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Completion c;
            c.kind = Kind::Manual;
            c.ok = false;
            c.showStatus = true;
            c.path = path;
            c.message = std::string("exception while queuing save: ") + e.what();
            m_done.push_back(std::move(c));
        }
    }

    void EnqueueAutosave(const proto::World& world,
                         const fs::path& dir,
                         int keepCount,
                         bool showStatus,
                         double playtimeSeconds)
    {
        // Clamp keep count so we don't spam the filesystem.
        keepCount = std::max(1, std::min(keepCount, 20));

        const std::uint64_t gen = m_autosaveGeneration.load(std::memory_order_acquire);
        try
        {
            Task t;
            t.kind = Kind::Autosave;
            t.pathOrDir = dir;
            t.keepCount = keepCount;
            t.autosaveGen = gen;
            t.pretty = false;
            t.showStatus = showStatus;
            t.snap = MakeSnapshot(world, playtimeSeconds);

            {
                std::lock_guard<std::mutex> lock(m_mutex);

                // Coalesce autosaves: keep only the newest queued autosave snapshot.
                // (Manual saves are preserved and will run before autosaves.)
                m_queue.erase(
                    std::remove_if(m_queue.begin(), m_queue.end(),
                                   [](const Task& q) { return q.kind == Kind::Autosave; }),
                    m_queue.end());

                m_queue.push_back(std::move(t));
            }
            m_cv.notify_one();
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Completion c;
            c.kind = Kind::Autosave;
            c.ok = false;
            c.showStatus = true;
            c.path = dir;
            c.message = std::string("exception while queuing autosave: ") + e.what();
            m_done.push_back(std::move(c));
        }
    }

    void DrainCompletions(std::vector<Completion>& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_done.empty())
            return;
        out.insert(out.end(), std::make_move_iterator(m_done.begin()), std::make_move_iterator(m_done.end()));
        m_done.clear();
    }

private:
    struct Task {
        Kind kind = Kind::Manual;
        Snapshot snap;
        fs::path pathOrDir;
        int keepCount = 5;
        std::uint64_t autosaveGen = 0;
        bool showStatus = false;
        bool pretty = false;
    };

    static Snapshot MakeSnapshot(const proto::World& world, double playtimeSeconds)
    {
        Snapshot s;
        s.w = world.width();
        s.h = world.height();

        const auto& inv = world.inventory();
        s.wood = inv.wood;
        s.food = inv.food;

        s.buildWorkPerSecond = world.buildWorkPerSecond;
        s.colonistWalkSpeed = world.colonistWalkSpeed;
        s.farmGrowDurationSeconds = world.farmGrowDurationSeconds;
    s.farmHarvestYieldFood = world.farmHarvestYieldFood;
    s.farmHarvestDurationSeconds = world.farmHarvestDurationSeconds;
        s.treeChopYieldWood = world.treeChopYieldWood;
        s.treeSpreadAttemptsPerSecond = world.treeSpreadAttemptsPerSecond;
        s.treeSpreadChancePerAttempt = world.treeSpreadChancePerAttempt;
        s.foodPerColonistPerSecond = world.foodPerColonistPerSecond;

        s.colonistMaxPersonalFood = world.colonistMaxPersonalFood;
        s.colonistEatThresholdFood = world.colonistEatThresholdFood;
        s.colonistEatDurationSeconds = world.colonistEatDurationSeconds;

        s.haulCarryCapacity = world.haulCarryCapacity;
        s.haulPickupDurationSeconds = world.haulPickupDurationSeconds;
        s.haulDropoffDurationSeconds = world.haulDropoffDurationSeconds;

            // v11+ pathfinding tuning
            s.pathAlgo = world.pathAlgo;
            s.pathCacheEnabled = world.pathCacheEnabled;
            s.pathCacheMaxEntries = world.pathCacheMaxEntries;
            s.navTerrainCostsEnabled = world.navUseTerrainCosts;

        // Summary counts (cheap, cached inside World).
        s.plannedCount    = world.plannedCount();
        s.builtFloors     = world.builtCount(proto::TileType::Floor);
        s.builtWalls      = world.builtCount(proto::TileType::Wall);
        s.builtFarms      = world.builtCount(proto::TileType::Farm);
        s.builtStockpiles = world.builtCount(proto::TileType::Stockpile);

        s.savedUnixSecondsUtc = UnixSecondsUtcNow();
        s.playtimeSeconds = playtimeSeconds;

        s.cells.resize(static_cast<std::size_t>(s.w * s.h));
        for (int y = 0; y < s.h; ++y)
        {
            for (int x = 0; x < s.w; ++x)
            {
                const proto::Cell& c = world.cell(x, y);
                Cell out;
                out.built = static_cast<std::uint8_t>(c.built);
                out.planned = static_cast<std::uint8_t>(c.planned);
                out.workRemaining = c.workRemaining;
                out.farmGrowth = c.farmGrowth;
                out.planPriority = c.planPriority;
                out.builtFromPlan = c.builtFromPlan ? 1u : 0u;
                out.looseWood = c.looseWood;
                s.cells[static_cast<std::size_t>(y * s.w + x)] = out;
            }
        }

        const auto& cols = world.colonists();
        s.colonists.reserve(cols.size());
        for (const auto& c : cols)
        {
            Colonist out;
            out.id = c.id;
            out.x = c.x;
            out.y = c.y;

            out.personalFood = c.personalFood;

            out.drafted = c.drafted;
            out.role = c.role.role;
            out.roleLevel = c.role.level;
            out.roleXp = static_cast<std::uint32_t>(c.role.xp);

            out.workPrioBuild = c.workPrio.build;
            out.workPrioFarm = c.workPrio.farm;
            out.workPrioHaul = c.workPrio.haul;

            s.colonists.push_back(out);
        }

        return s;
    }

    static fs::path AutosavePathForIndex(const fs::path& dir, int index) noexcept
    {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "autosave_%02d.json", index);
        return dir / buf;
    }

    static void RotateAutosaves(const fs::path& dir, int keepCount) noexcept
    {
        std::error_code ec;

        for (int i = keepCount - 1; i >= 1; --i)
        {
            const fs::path dst = AutosavePathForIndex(dir, i);
            const fs::path src = AutosavePathForIndex(dir, i - 1);

            ec.clear();
            if (!fs::exists(src, ec) || ec)
                continue;

            // Best-effort replace (handles transient Windows locks from scanners/Explorer).
            (void)winpath::remove_with_retry(dst);

            std::error_code rn;
            if (!winpath::rename_with_retry(src, dst, &rn)) // keep meta file paired with the world file
                continue;

            // Sidecar meta file (optional).
            fs::path dstMeta = dst;
            dstMeta.replace_extension(".meta.json");
            fs::path srcMeta = src;
            srcMeta.replace_extension(".meta.json");

            ec.clear();
            if (fs::exists(srcMeta, ec) && !ec)
            {
                (void)winpath::remove_with_retry(dstMeta);

                std::error_code rnMeta;
                (void)winpath::rename_with_retry(srcMeta, dstMeta, &rnMeta);
            }
        }
    }

    static bool WriteSnapshotJson(const Snapshot& s, Kind kind, const fs::path& path, bool pretty, std::string& outErr)
    {
        try
        {
            json j;
            j["format"] = proto::savefmt::kWorldFormat;
            j["version"] = proto::savefmt::kWorldVersion;
            j["size"] = { {"w", s.w}, {"h", s.h} };
            j["inventory"] = { {"wood", s.wood}, {"food", s.food} };
            j["tuning"] = {
                {"buildWorkPerSecond", s.buildWorkPerSecond},
                {"colonistWalkSpeed", s.colonistWalkSpeed},
                {"farmGrowDurationSeconds", s.farmGrowDurationSeconds},
                {"farmHarvestYieldFood", s.farmHarvestYieldFood},
                {"farmHarvestDurationSeconds", s.farmHarvestDurationSeconds},
                {"treeChopYieldWood", s.treeChopYieldWood},
                {"treeSpreadAttemptsPerSecond", s.treeSpreadAttemptsPerSecond},
                {"treeSpreadChancePerAttempt", s.treeSpreadChancePerAttempt},
                {"foodPerColonistPerSecond", s.foodPerColonistPerSecond},
                {"colonistMaxPersonalFood", s.colonistMaxPersonalFood},
                {"colonistEatThresholdFood", s.colonistEatThresholdFood},
                {"colonistEatDurationSeconds", s.colonistEatDurationSeconds},

                // v8+: hauling tuning
                {"haulCarryCapacity", s.haulCarryCapacity},
                {"haulPickupDurationSeconds", s.haulPickupDurationSeconds},
                {"haulDropoffDurationSeconds", s.haulDropoffDurationSeconds},

                // v11+: pathfinding tuning
                {"pathfindingAlgorithm", std::string{proto::PathAlgoName(s.pathAlgo)}},
                {"pathCacheEnabled", s.pathCacheEnabled},
                {"pathCacheMaxEntries", s.pathCacheMaxEntries},
                {"navTerrainCostsEnabled", s.navTerrainCostsEnabled},
            };

            json cells = json::array();
            // nlohmann::json does not expose reserve() directly; reserve the underlying
            // array storage for faster writes when serializing large worlds.
            cells.get_ref<json::array_t&>().reserve(s.cells.size());
            for (const Cell& c : s.cells)
            {
                cells.push_back({
                    static_cast<int>(c.built),
                    static_cast<int>(c.planned),
                    c.workRemaining,
                    static_cast<int>(c.planPriority),
                    static_cast<int>(c.builtFromPlan),
                    c.farmGrowth,
                    c.looseWood,
                });
            }
            j["cells"] = std::move(cells);

            json colonists = json::array();
            colonists.get_ref<json::array_t&>().reserve(s.colonists.size());
            for (const Colonist& c : s.colonists)
                colonists.push_back({
                    {"id", c.id},
                    {"x", c.x},
                    {"y", c.y},

                    // v7+: roles + drafted state
                    {"drafted", c.drafted},
                    {"personalFood", c.personalFood},
                    {"role", RoleDefOf(c.role).name},
                    {"roleLevel", static_cast<int>(c.roleLevel)},
                    {"roleXp", static_cast<std::uint32_t>(c.roleXp)},

                    // v9+: work priorities
                    {"workPriorities", {
                        {"build", static_cast<int>(c.workPrioBuild)},
                        {"farm", static_cast<int>(c.workPrioFarm)},
                        {"haul", static_cast<int>(c.workPrioHaul)},
                    }},
                });
            j["colonists"] = std::move(colonists);

            const std::string bytes = pretty ? j.dump(2) : j.dump();

            // Ensure dirs exist. (Atomic write uses a temp file in the same directory.)
            std::error_code ec;
            const fs::path parent = path.has_parent_path() ? path.parent_path() : fs::path();
            if (!parent.empty())
                fs::create_directories(parent, ec);

            std::error_code wec;
            if (!winpath::atomic_write_file(path, bytes.data(), bytes.size(), &wec))
            {
                outErr = std::string("atomic_write_file failed for ") + colony::util::PathToUtf8String(path);
                if (wec)
                {
                    outErr += ": ";
                    outErr += wec.message();
                    outErr += " (code ";
                    outErr += std::to_string(wec.value());
                    outErr += ")";
                }
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            outErr = e.what();
            return false;
        }
    }

    static bool WriteSnapshotMetaJson(const Snapshot& s,
                                     Kind kind,
                                     const fs::path& metaPath,
                                     bool pretty,
                                     std::string& outErr)
    {
        try
        {
            json j;
            j["format"] = "colony_proto_world_meta";
            j["version"] = 1;

            j["world"] = { {"w", s.w}, {"h", s.h} };
            j["inventory"] = { {"wood", s.wood}, {"food", s.food} };

            j["counts"] = {
                {"population", static_cast<int>(s.colonists.size())},
                {"planned", s.plannedCount},
                {"built", {
                    {"Floor", s.builtFloors},
                    {"Wall", s.builtWalls},
                    {"Farm", s.builtFarms},
                    {"Stockpile", s.builtStockpiles},
                }},
            };

            j["meta"] = {
                {"kind", (kind == Kind::Autosave) ? "autosave" : "manual"},
                {"savedUnixSecondsUtc", s.savedUnixSecondsUtc},
                {"playtimeSeconds", s.playtimeSeconds},
            };

            // Tiny world thumbnail (for save browser previews).
            // Packed bytes: low nibble = built TileType, high nibble = planned TileType.
            {
                constexpr int kMaxDim = 64;
                const int tw = std::max(1, std::min(s.w, kMaxDim));
                const int th = std::max(1, std::min(s.h, kMaxDim));

                const std::size_t worldCount = static_cast<std::size_t>(s.w) * static_cast<std::size_t>(s.h);
                const std::size_t thumbCount = static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);

                if (s.w > 0 && s.h > 0 && s.cells.size() == worldCount && thumbCount > 0)
                {
                    std::vector<std::uint8_t> thumb;
                    thumb.resize(thumbCount);

                    for (int y = 0; y < th; ++y)
                    {
                        const int wy = std::clamp((y * s.h + th / 2) / th, 0, s.h - 1);
                        for (int x = 0; x < tw; ++x)
                        {
                            const int wx = std::clamp((x * s.w + tw / 2) / tw, 0, s.w - 1);
                            const Cell& c = s.cells[static_cast<std::size_t>(wy * s.w + wx)];
                            const std::uint8_t built = static_cast<std::uint8_t>(c.built) & 0x0Fu;
                            const std::uint8_t planned = static_cast<std::uint8_t>(c.planned) & 0x0Fu;
                            thumb[static_cast<std::size_t>(y * tw + x)] = static_cast<std::uint8_t>((planned << 4) | built);
                        }
                    }

                    j["thumb"] = {
                        {"w", tw},
                        {"h", th},
                        {"encoding", "base64_u8"},
                        {"data", save::Base64Encode(thumb)},
                    };
                }
            }

            const std::string bytes = pretty ? j.dump(2) : j.dump();

            // Ensure dirs exist. (Atomic write uses a temp file in the same directory.)
            std::error_code ec;
            const fs::path parent = metaPath.has_parent_path() ? metaPath.parent_path() : fs::path();
            if (!parent.empty())
                fs::create_directories(parent, ec);

            std::error_code wec;
            if (!winpath::atomic_write_file(metaPath, bytes.data(), bytes.size(), &wec))
            {
                outErr = std::string("atomic_write_file failed for ") + colony::util::PathToUtf8String(metaPath);
                if (wec)
                {
                    outErr += ": ";
                    outErr += wec.message();
                    outErr += " (code ";
                    outErr += std::to_string(wec.value());
                    outErr += ")";
                }
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            outErr = e.what();
            return false;
        }
    }


    void workerMain() noexcept
    {
        // Background worker: keep it debuggable + low impact on frame time.
        // - Name thread (Visual Studio / ETW)
        // - Lower base priority so input/render remain responsive
        {
            using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);

            if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
            {
                auto pSetThreadDescription =
                    reinterpret_cast<SetThreadDescription_t>(
                        ::GetProcAddress(k32, "SetThreadDescription"));

                if (pSetThreadDescription)
                    (void)pSetThreadDescription(::GetCurrentThread(), L"AsyncSave");
            }
        }

        (void)::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&] { return m_stop.load(std::memory_order_acquire) || !m_queue.empty(); });

                if (m_stop.load(std::memory_order_acquire) && m_queue.empty())
                    return;
                if (m_queue.empty())
                    continue;

                task = std::move(m_queue.front());
                m_queue.pop_front();
            }

            // Stale autosaves are skipped entirely (prevents "old world" from writing after a load/reset).
            if (task.kind == Kind::Autosave)
            {
                const std::uint64_t genNow = m_autosaveGeneration.load(std::memory_order_acquire);
                if (task.autosaveGen != genNow)
                    continue;
            }

            // Ensure standard folders exist.
            winpath::ensure_dirs();

            Completion c;
            c.kind = task.kind;
            c.showStatus = task.showStatus;

            std::string err;
            bool ok = false;

            if (task.kind == Kind::Autosave)
            {
                const fs::path dir = task.pathOrDir;
                RotateAutosaves(dir, task.keepCount);

                // If a load/reset happened while we were rotating autosaves, don't write a stale snapshot.
                const std::uint64_t genNow2 = m_autosaveGeneration.load(std::memory_order_acquire);
                if (task.autosaveGen != genNow2)
                    continue;

                c.path = AutosavePathForIndex(dir, 0);
                ok = WriteSnapshotJson(task.snap, Kind::Autosave, c.path, /*pretty=*/false, err);

                // Sidecar meta (best-effort; save is still considered successful if this fails).
                if (ok)
                {
                    fs::path metaPath = c.path;
                    metaPath.replace_extension(".meta.json");
                    std::string metaErr;
                    (void)WriteSnapshotMetaJson(task.snap, Kind::Autosave, metaPath, /*pretty=*/false, metaErr);
                }

                // Only report autosave successes if explicitly requested; always report failures.
                if (ok && !c.showStatus)
                {
                    continue;
                }
            }
            else
            {
                c.path = task.pathOrDir;
                ok = WriteSnapshotJson(task.snap, Kind::Manual, c.path, task.pretty, err);

                // Sidecar meta (best-effort; save is still considered successful if this fails).
                if (ok)
                {
                    fs::path metaPath = c.path;
                    metaPath.replace_extension(".meta.json");
                    std::string metaErr;
                    (void)WriteSnapshotMetaJson(task.snap, Kind::Manual, metaPath, task.pretty, metaErr);
                }
            }

            c.ok = ok;
            if (!ok)
                c.message = err.empty() ? "unknown error" : err;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_done.push_back(std::move(c));
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    std::vector<Completion> m_done;
    std::thread m_thread;
    std::atomic_bool m_stop{false};
    std::atomic<std::uint64_t> m_autosaveGeneration{1};
};


void AsyncSaveManagerDeleter::operator()(AsyncSaveManager* p) const noexcept
{
    delete p;
}

// NOTE: PrototypeGame::Impl is defined in PrototypeGame_Impl.h but owns a
// std::unique_ptr<AsyncSaveManager> where AsyncSaveManager is defined above.
// Defining the destructor out-of-line here ensures MSVC instantiates the
// unique_ptr deleter with a complete type.
PrototypeGame::Impl::~Impl() = default;

fs::path PrototypeGame::Impl::worldSaveDir() const
{
    // Ensure standard folders exist. It's cheap and makes save/load resilient.
    winpath::ensure_dirs();
    return winpath::saved_games_dir();
}

fs::path PrototypeGame::Impl::defaultWorldSavePath() const
{
    // Prefer the user's "Saved Games" folder for quick iteration and easy discovery.
    // PathUtilWin falls back to LocalAppData if the Saved Games folder isn't available.
    return worldSavePathForSlot(0);
}

fs::path PrototypeGame::Impl::worldSavePathForSlot(int slot) const
{
    // Slot 0 is the legacy/default location.
    if (slot <= 0)
        return worldSaveDir() / "proto_world.json";

    if (slot > 9)
        slot = 9;

    return worldSaveDir() / ("proto_world_slot_" + std::to_string(slot) + ".json");
}

fs::path PrototypeGame::Impl::autosavePathForIndex(int index) const
{
    // autosave_00 is always "newest"; we rotate older saves upward.
    const int keep = std::max(1, std::min(autosaveKeepCount, 20));
    index = std::max(0, std::min(keep - 1, index));

    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "autosave_%02d.json", index);
    return worldSaveDir() / buf;
}

bool PrototypeGame::Impl::saveWorldToPath(const fs::path& path, bool showStatus)
{
    if (path.empty())
        return false;

    if (!saveMgr)
        saveMgr = AsyncSaveManagerPtr{ new AsyncSaveManager() };

    saveMgr->EnqueueManualSave(world, path, showStatus, playtimeSeconds);

    if (showStatus)
        setStatus("Saving...", 1.0f);

    return true;
}

bool PrototypeGame::Impl::loadWorldFromPath(const fs::path& path, bool showStatus)
{
    // Prevent a queued autosave from the "old" world overwriting the newest autosave
    // after we load/reset.
    invalidatePendingAutosaves();

    std::string err;
    if (!world.LoadJson(path, &err))
    {
        if (!fs::exists(path))
            err = std::string("no save found at ") + colony::util::PathToUtf8String(path);
        else if (err.empty())
            err = "unknown error";

        setStatus(std::string("Load failed: ") + err, 4.f);
        return false;
    }

    // Avoid "stuck drag" behavior and stale paint state after a load that may change world size.
    clearPlanHistory();

    // Clear selection state (tile + colonist) — the loaded world may have
    // different dimensions/contents.
    selectedX = -1;
    selectedY = -1;
    selectedColonistId = -1;
    followSelectedColonist = false;

    // Keep the reset UI in sync with the loaded size.
    worldResetW = world.width();
    worldResetH = world.height();

    // Recentering the camera makes loading feel less confusing if the world size changed.
    const auto& s = camera.State();
    const float cx = std::max(0.0f, static_cast<float>(world.width()) * 0.5f);
    const float cy = std::max(0.0f, static_cast<float>(world.height()) * 0.5f);
    (void)camera.ApplyPan(cx - s.panX, cy - s.panY);

    // Prevent immediate autosave right after a load.
    autosaveAccumSeconds = 0.f;

    if (showStatus)
        setStatus(std::string("World loaded: ") + colony::util::PathToUtf8String(path), 3.f);

    return true;
}

bool PrototypeGame::Impl::autosaveWorld()
{
    if (!autosaveEnabled)
        return false;

    // Clamp keep count to something reasonable so we don't spam the filesystem.
    autosaveKeepCount = std::max(1, std::min(autosaveKeepCount, 20));

    if (!saveMgr)
        saveMgr = AsyncSaveManagerPtr{ new AsyncSaveManager() };

    // Autosave runs on the background thread (rotation + write).
    saveMgr->EnqueueAutosave(world,
                             worldSaveDir(),
                             autosaveKeepCount,
                             /*showStatus=*/false,
                             playtimeSeconds);
    return true;
}

void PrototypeGame::Impl::pollAsyncSaves() noexcept
{
    if (!saveMgr)
        return;

    std::vector<AsyncSaveManager::Completion> done;
    saveMgr->DrainCompletions(done);

    if (!done.empty())
        saveBrowserDirty = true;

    for (const auto& c : done)
    {
        if (!c.ok)
        {
            const char* kind = (c.kind == AsyncSaveManager::Kind::Autosave) ? "Autosave" : "Save";
            setStatus(std::string(kind) + " failed: " + (c.message.empty() ? "unknown error" : c.message), 4.f);
            continue;
        }

        if (c.showStatus)
        {
            if (c.kind == AsyncSaveManager::Kind::Autosave)
                setStatus(std::string("Autosaved: ") + colony::util::PathToUtf8String(c.path), 2.0f);
            else
                setStatus(std::string("World saved: ") + colony::util::PathToUtf8String(c.path), 3.0f);
        }
    }
}

void PrototypeGame::Impl::invalidatePendingAutosaves() noexcept
{
    if (saveMgr)
        saveMgr->BumpAutosaveGeneration();
}

bool PrototypeGame::Impl::saveWorld()
{
    return saveWorldToPath(defaultWorldSavePath(), /*showStatus=*/true);
}

bool PrototypeGame::Impl::loadWorld()
{
    return loadWorldFromPath(defaultWorldSavePath(), /*showStatus=*/true);
}

} // namespace colony::game
