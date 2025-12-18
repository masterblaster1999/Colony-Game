// src/pch.hpp
#pragma once

// --------------------------------------------------------------------------------------
// IMPORTANT (Unity-build safety):
// MSVC error C2872: 'byte': ambiguous symbol commonly happens when a TU contains
// `using namespace std;` before Windows SDK COM headers (objidl.h/oaidl.h) are parsed.
// Unity builds make this more likely because multiple .cpp files share one TU.
// Fix strategy:
//   - Never put `using namespace std;` in headers (especially not in PCH).
//   - Pre-include the problematic Windows COM headers here so they are parsed early.
//     With CMake PCH, this header is force-included (/FI on MSVC), so later includes
//     in unity blobs won't re-parse these headers after any using-directives.
// --------------------------------------------------------------------------------------

#include <cstdint>
#include <cstddef> // size_t, std::byte (keep it; do NOT rely on `using namespace std;`)
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <span>
#include <chrono>

#ifdef _WIN32
  // Defensive: ensure these are set even if a target forgets to add them as compile definitions.
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif

  // Pre-include the COM headers that were failing in your build log:
  //   - objidl.h / oaidl.h contain unqualified `byte` usages.
  // If they get parsed after a TU-level `using namespace std;`, `byte` can become ambiguous
  // between RPC's `::byte` and `std::byte`.
  #include <objidl.h>
  #include <oaidl.h>
#endif
