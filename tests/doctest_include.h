#pragma once

#if defined(DOCTEST_CONFIG_IMPLEMENT) || defined(DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    #error "Do not define DOCTEST_CONFIG_IMPLEMENT* here. Only tests/test_main.cpp may implement doctest."
#endif

#include <doctest/doctest.h>
