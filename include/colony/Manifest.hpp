#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <filesystem>

namespace colony::data {

namespace fs = std::filesystem;

struct DataRecord {
    std::optional<std::string> id;    // may be null in manifest
    std::string type;
    fs::path    relPath;              // e.g. "data/items/sword.json"
    std::string sha256;
    std::vector<std::string> deps;
    bool        hasSchema{false};
    std::optional<std::string> schema;
};

struct AssetRecord {
    fs::path    relPath;              // e.g. "resources/ui/atlas.png"
    std::string sha256;
    std::uintmax_t sizeBytes{};
};

struct Manifest {
    fs::path rootGuess;               // resolved project root used for path resolution
    std::vector<DataRecord>  data;
    std::vector<AssetRecord> assets;

    // Fast lookups
    std::unordered_map<std::string, std::size_t> indexById;     // id -> data[index]
    std::unordered_map<std::string, std::size_t> indexAssetByRel; // rel string -> assets[index]

    const DataRecord* findDataById(std::string_view id) const {
        auto it = indexById.find(std::string(id));
        return (it == indexById.end()) ? nullptr : &data[it->second];
    }

    // Resolve absolute on disk path for a data or asset entry.
    fs::path resolvePath(const DataRecord& rec) const { return rootGuess / rec.relPath; }
    fs::path resolvePath(const AssetRecord& rec) const { return rootGuess / rec.relPath; }
};

class ManifestLoader {
public:
    // Locate manifest using environment override, typical build folders, and upward search.
    static fs::path Locate();

    // Load + build indices. If you already know the manifest path, pass it here.
    static Manifest Load(const fs::path& manifestPath);

private:
    static fs::path findRootNear(const fs::path& startDir); // climbs up to find a folder with "data" present
    static void     buildIndices(Manifest& m);
};

} // namespace colony::data
