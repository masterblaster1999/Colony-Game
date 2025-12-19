// src/core/ecs/components/Destroy.hpp
#pragma once

// NOTE:
// `comp::Destroy` is a tag component defined in `core/ecs/Tags.h`.
// This header exists to preserve the include path `core/ecs/components/Destroy.hpp`
// without defining the type twice (Unity builds will otherwise fail).

#include "core/ecs/Tags.h"
