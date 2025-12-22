#pragma once

// Shim header: some targets currently reference "src/pch.hpp" while already being
// inside the "src" directory, which results in "src/src/pch.hpp" on Windows.
// This file forwards to the real precompiled header.

#include "../pch.hpp"
