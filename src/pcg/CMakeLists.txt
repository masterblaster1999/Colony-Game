# Header-only library for the Poisson sampler
cmake_minimum_required(VERSION 3.16)

add_library(pcg_poisson INTERFACE)
# Make "pcg/PoissonDisk2D.hpp" available as #include "pcg/PoissonDisk2D.hpp"
target_include_directories(pcg_poisson INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(pcg_poisson INTERFACE cxx_std_17)
