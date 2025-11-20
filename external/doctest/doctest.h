// external/doctest/doctest.h
//
// Thin wrapper for the official doctest single-header library.
// The real doctest header must be placed next to this file as
// "doctest_impl.h" or fetched via a submodule / script.

#pragma once

// If you want to vendor doctest directly, download the official
// header from:
//   https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
// and save it as "doctest_impl.h" in this same directory:
//
//   external/doctest/doctest_impl.h
//
// Then include this wrapper everywhere instead of <doctest/doctest.h>.

#include "doctest_impl.h"
