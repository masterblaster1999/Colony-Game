#pragma once

#include "procgen/WorldGen.h"

namespace procgen {

// After the base noise fields are generated, call this to:
//  - choose a RegionKind for each coarse region cell
//  - label each tile with its RegionKind
//  - lightly tweak moisture & temperature to fit the archetype
void assign_regions(GeneratedWorld& world, const WorldGenParams& params);

} // namespace procgen
