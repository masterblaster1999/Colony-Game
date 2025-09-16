#pragma once
#include <filesystem>
#include <string>

namespace cg::io {
    // Writes to <path>.tmp, flushes, then atomically replaces target (and optional .bak)
    // Returns true on success; on error fills 'err'
    bool write_atomic(const std::filesystem::path& path,
                      const std::string& bytes,
                      std::string* err = nullptr,
                      bool make_backup = true);

    // Reads entire file into 'out'; returns false if open fails
    bool read_all(const std::filesystem::path& path,
                  std::string& out,
                  std::string* err = nullptr);
}
