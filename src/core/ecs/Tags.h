#pragma once
namespace tag {
    struct NewlySpawned {};    // lifetime: one frame
    struct DirtyNavmesh {};    // set by terrain changes
}
