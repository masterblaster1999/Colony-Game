#pragma once

// Minimal tag component used to mark entities for deferred destruction.
// Keep it empty to ensure trivially copyable/movable and zero-cost storage.
namespace comp {
    struct Destroy {};
}
