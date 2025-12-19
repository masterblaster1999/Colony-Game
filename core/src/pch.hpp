// core/src/pch.hpp
#pragma once

// Keep this very stable: includes here should rarely change,
// otherwise you’ll nuke incremental build times.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <optional>
#include <chrono>

// Include your most stable “core” headers last (if you have one):
// #include "core/Config.hpp"
// #include "core/Types.hpp"
