#pragma once
#include <cstdint>
#include <filesystem>

namespace winpath {
    bool atomic_write_file(const std::filesystem::path& dst,
                           const void* data,
                           std::uint64_t size) noexcept;
}
