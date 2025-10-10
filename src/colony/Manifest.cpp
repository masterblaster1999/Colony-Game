#include "Colony/Manifest.hpp"
#include <fstream>
#include <system_error>
#include <cstdlib> // std::getenv
#include <nlohmann/json.hpp>

namespace colony::data {
namespace fs = std::filesystem;
using nlohmann::json;

static bool looks_like_build_dir_name(const fs::path& p) {
    auto name = p.filename().string();
    return name == "build" || name == "Build" || name == "out" || name == "bin";
}

fs::path ManifestLoader::Locate() {
    // 1) Explicit env override (absolute or relative)
    if (const char* env = std::getenv("COLONY_MANIFEST")) {
        fs::path p = fs::path(env);
        if (fs::exists(p)) return fs::absolute(p);
    }

    // 2) Common relative spots from CWD (useful when running from build/)
    const fs::path cwd = fs::current_path();
    const std::vector<fs::path> candidates = {
        cwd / "build" / "data_manifest.json",
        cwd / "data_manifest.json",
        cwd / ".."   / "build" / "data_manifest.json",
        cwd / ".."   / "data_manifest.json"
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return fs::absolute(c);
    }

    // 3) Upward search for a file literally named data_manifest.json
    fs::path p = cwd;
    for (int i = 0; i < 6 && p.has_parent_path(); ++i) {
        auto probe = p / "data_manifest.json";
        if (fs::exists(probe)) return fs::absolute(probe);
        p = p.parent_path();
    }

    throw std::runtime_error("Could not locate data_manifest.json. "
                             "Set COLONY_MANIFEST=/path/to/data_manifest.json");
}

// Walk up until we see a folder that actually contains "data"
fs::path ManifestLoader::findRootNear(const fs::path& startDir) {
    fs::path p = startDir;
    for (int i = 0; i < 6 && p.has_parent_path(); ++i) {
        if (fs::exists(p / "data") && fs::is_directory(p / "data")) {
            return fs::absolute(p);
        }
        // If the manifest is under .../build/, jump one level up quickly.
        if (looks_like_build_dir_name(p.filename())) {
            auto up = p.parent_path();
            if (fs::exists(up / "data") && fs::is_directory(up / "data")) {
                return fs::absolute(up);
            }
        }
        p = p.parent_path();
    }
    return fs::absolute(startDir);
}

void ManifestLoader::buildIndices(Manifest& m) {
    m.indexById.clear();
    for (std::size_t i = 0; i < m.data.size(); ++i) {
        if (m.data[i].id) m.indexById[*m.data[i].id] = i;
    }
    m.indexAssetByRel.clear();
    for (std::size_t i = 0; i < m.assets.size(); ++i) {
        m.indexAssetByRel[m.assets[i].relPath.generic_string()] = i;
    }
}

Manifest ManifestLoader::Load(const fs::path& manifestPath) {
    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open manifest: " + manifestPath.string());
    }

    json j;
    in >> j;

    Manifest m{};
    // Heuristic root: from manifest location, walk upwards until a folder that contains "data/"
    // (works regardless of whether manifest lives in <root>/build or next to the binary).
    const fs::path manifestDir = manifestPath.parent_path();
    m.rootGuess = findRootNear(manifestDir);

    if (j.contains("data") && j["data"].is_array()) {
        for (const json& item : j["data"]) {
            DataRecord rec{};
            if (item.contains("id") && !item["id"].is_null())
                rec.id = item["id"].get<std::string>();
            rec.type     = item.value("type", "");
            rec.relPath  = fs::path(item.value("path", "")); // stored as repo-relative in manifest
            rec.sha256   = item.value("sha256", "");
            rec.deps     = item.value("deps", std::vector<std::string>{});
            rec.hasSchema= item.value("has_schema", false);
            if (item.contains("schema") && !item["schema"].is_null())
                rec.schema = item["schema"].get<std::string>();
            m.data.emplace_back(std::move(rec));
        }
    }

    if (j.contains("assets") && j["assets"].is_array()) {
        for (const json& item : j["assets"]) {
            AssetRecord ar{};
            ar.relPath   = fs::path(item.value("path", ""));
            ar.sha256    = item.value("sha256", "");
            // Some filesystems may not carry size in manifest; keep it optional-ish.
            if (item.contains("size_bytes")) {
                ar.sizeBytes = item.value("size_bytes", static_cast<std::uintmax_t>(0));
            }
            m.assets.emplace_back(std::move(ar));
        }
    }

    buildIndices(m);
    return m;
}

} // namespace colony::data
