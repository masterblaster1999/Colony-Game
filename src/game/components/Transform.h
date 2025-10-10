#pragma once

// Transform shim → tries common include paths first, then provides a minimal fallback.
// Add your actual path below and it will be picked up without touching tests again.

#if __has_include("ecs/components/Transform.h")
  #include "ecs/components/Transform.h"
#elif __has_include("engine/ecs/Transform.h")
  #include "engine/ecs/Transform.h"
#elif __has_include("components/Transform.h")
  #include "components/Transform.h"
#else
  // ---- Minimal fallback (only for tests / compile sanity) ----
  struct Transform {
    float position[3]{0,0,0};
    float rotation[4]{0,0,0,1}; // quaternion
    float scale[3]{1,1,1};
  };
#endif
