#pragma once
#include "worldgen/WorldGen.hpp"      // StageId + IWorldGenStage
#include "worldgen/StageContext.hpp"  // StageContext
#include "worldgen/WorldChunk.hpp"    // <-- provide complete type for ctx.out.*
#include "worldgen/Math.hpp"          // lerp/smoothstep
#include <algorithm>
#include <cmath>
#include <cstdint>
