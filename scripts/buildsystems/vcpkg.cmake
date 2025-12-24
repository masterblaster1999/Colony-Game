# D:/a/Colony-Game/Colony-Game/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake_minimum_required(VERSION 3.21)

# Expect a global vcpkg installation pointed to by VCPKG_ROOT
if(NOT DEFINED ENV{VCPKG_ROOT})
  message(FATAL_ERROR "VCPKG_ROOT is not set. Install vcpkg and set the environment variable.")
endif()

set(_vcpkg_toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

if(NOT EXISTS "${_vcpkg_toolchain}")
  message(FATAL_ERROR "Could not find vcpkg toolchain at '${_vcpkg_toolchain}'.")
endif()

# Delegate to the real vcpkg toolchain
include("${_vcpkg_toolchain}")
