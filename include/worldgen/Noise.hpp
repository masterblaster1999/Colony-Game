#pragma once
// Public forwarding header.
//
// The engine's CPU worldgen noise implementation lives under src/worldgen.
// This wrapper avoids duplicate/placeholder implementations (and /WX warnings)
// by forwarding to the real declarations.
#include "../../src/worldgen/Noise.hpp"
