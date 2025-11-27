#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "colony/memory/arena.h"
#include "colony/memory/pool.h"

TEST_CASE("Arena basic allocate/reset") {
    colony::memory::Arena a(1<<16);
    auto* p1 = a.allocate(64);
    auto* p2 = a.allocate(128, 64);
    CHECK(p1 != nullptr);
    CHECK(p2 != nullptr);
    a.reset(); // everything freed en masse
    auto* p3 = a.allocate(32);
    CHECK(p3 != nullptr);
}

struct Foo { int x; explicit Foo(int v):x(v){} };

TEST_CASE("Arena make<T> constructs in-place") {
    colony::memory::Arena a;
    auto* f = a.make<Foo>(42);
    CHECK(f->x == 42);
    a.reset();
}

TEST_CASE("ObjectPool create/destroy") {
    colony::memory::ObjectPool<Foo, 8, false> pool;
    std::vector<Foo*> ptrs;
    for (int i = 0; i < 16; ++i) ptrs.push_back(pool.create(i));
    for (int i = 0; i < 16; ++i) {
        CHECK(ptrs[i]->x == i);
        pool.destroy(ptrs[i]);
    }
}
