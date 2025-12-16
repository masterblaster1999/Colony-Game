#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "colony/memory/arena.h"
#include "colony/memory/pool.h"

TEST_CASE("Arena basic allocate/reset") {
    constexpr std::size_t kArenaSize = static_cast<std::size_t>(1u) << 16; // 64 KiB
    colony::memory::Arena a(kArenaSize);

    void* p1 = a.allocate(64);
    void* p2 = a.allocate(128, 64);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);

    // If the API accepts an alignment parameter, the returned pointer should satisfy it.
    CHECK((reinterpret_cast<std::uintptr_t>(p2) % 64u) == 0u);

    a.reset(); // everything freed en masse

    void* p3 = a.allocate(32);
    REQUIRE(p3 != nullptr);
}

struct Foo {
    int x;
    explicit Foo(int v) : x(v) {}
};

TEST_CASE("Arena make<T> constructs in-place") {
    colony::memory::Arena a;

    Foo* f = a.make<Foo>(42);
    REQUIRE(f != nullptr);
    CHECK(f->x == 42);

    a.reset();
}

TEST_CASE("ObjectPool create/destroy") {
    colony::memory::ObjectPool<Foo, 8, false> pool;

    std::vector<Foo*> ptrs;
    ptrs.reserve(16);

    for (int i = 0; i < 16; ++i) {
        Foo* p = pool.create(i);
        REQUIRE(p != nullptr);
        ptrs.push_back(p);
    }

    for (int i = 0; i < 16; ++i) {
        REQUIRE(ptrs[i] != nullptr);
        CHECK(ptrs[i]->x == i);
        pool.destroy(ptrs[i]);
    }
}
