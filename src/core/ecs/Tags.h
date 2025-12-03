// src/core/ecs/Tags.h  (or your existing Tags header)
// If these exist already, keep them; otherwise add them.
#pragma once

namespace comp {
    // Empty tag component to mark entities for deferred destruction.
    struct Destroy { };
}

namespace tag {
    struct NewlySpawned { };
    // ... your other tag definitions ...
}
