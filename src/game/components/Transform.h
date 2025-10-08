#pragma once

// This is a compatibility shim for the tests that include
//   #include "game/components/Transform.h"
// It tries common locations and extensions without forcing test changes.

#if __has_include("components/Transform.hpp")
  #include "components/Transform.hpp"
#elif __has_include("components/Transform.h")
  #include "components/Transform.h"
#elif __has_include("../../components/Transform.hpp")
  #include "../../components/Transform.hpp"
#elif __has_include("../../components/Transform.h")
  #include "../../components/Transform.h"
#elif __has_include("../../ecs/components/Transform.hpp")
  #include "../../ecs/components/Transform.hpp"
#elif __has_include("../../ecs/components/Transform.h")
  #include "../../ecs/components/Transform.h"
#else
  #error "Transform component header not found. Update this shim with the actual path."
#endif
