// tests/test_main.cpp
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

int main(int argc, char** argv) {
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);

    // Example: make CI failures faster
    // ctx.setOption("abort-after", 1);

    return ctx.run();
}
