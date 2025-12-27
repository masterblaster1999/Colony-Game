#include "doctest/doctest.h"

#include "game/proto/ProtoWorld.h"
#include "game/proto/ProtoWorld_SaveFormat.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {
namespace fs = std::filesystem;

fs::path MakeUniqueTempPath(const char* stem, const char* ext)
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path();
    p /= std::string(stem) + "_" + std::to_string(static_cast<long long>(now)) + ext;
    return p;
}

bool ReadFileToString(const fs::path& p, std::string& out)
{
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz < 0)
        return false;
    f.seekg(0, std::ios::beg);

    out.resize(static_cast<std::size_t>(sz));
    f.read(out.data(), static_cast<std::streamsize>(sz));
    return (f.gcount() == static_cast<std::streamsize>(sz));
}

bool WriteStringToFile(const fs::path& p, const std::string& bytes)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

void RemoveQuiet(const fs::path& p) noexcept
{
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

TEST_CASE("ProtoWorld SaveJson format/version and legacy format compatibility")
{
    using colony::proto::World;
    using colony::proto::savefmt::kWorldFormat;
    using colony::proto::savefmt::kWorldFormatLegacy;
    using colony::proto::savefmt::kWorldVersion;

    const fs::path pNew = MakeUniqueTempPath("colony_proto_world", ".json");
    const fs::path pOld = MakeUniqueTempPath("colony_proto_world_legacy", ".json");

    struct Cleanup {
        fs::path a;
        fs::path b;
        ~Cleanup() { RemoveQuiet(a); RemoveQuiet(b); }
    } cleanup{pNew, pOld};

    World w(8, 6, /*seed*/ 123);
    w.inventory().wood = 42;
    w.inventory().food = 13.5f;

    // A tiny bit of state so the file isn't totally trivial.
    (void)w.placePlan(1, 1, colony::proto::TileType::Wall, /*priority*/ 2);

    std::string err;
    REQUIRE_MESSAGE(w.SaveJson(pNew, &err), err);

    std::string bytes;
    REQUIRE(ReadFileToString(pNew, bytes));

    const nlohmann::json j = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    REQUIRE(j.is_object());

    CHECK(j.value("format", "") == std::string(kWorldFormat));
    CHECK(j.value("version", 0) == kWorldVersion);

    // Rewrite the same payload with the legacy format string.
    nlohmann::json jLegacy = j;
    jLegacy["format"] = kWorldFormatLegacy;

    REQUIRE(WriteStringToFile(pOld, jLegacy.dump(2)));

    World loaded;
    REQUIRE_MESSAGE(loaded.LoadJson(pOld, &err), err);

    CHECK(loaded.width() == w.width());
    CHECK(loaded.height() == w.height());
    CHECK(loaded.inventory().wood == w.inventory().wood);

}
