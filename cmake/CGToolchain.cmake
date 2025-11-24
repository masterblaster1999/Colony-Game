# cmake/CGToolchain.cmake – Windows only, repo-local vcpkg
if (WIN32)
  set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/vcpkg" CACHE PATH "Path to vcpkg root" FORCE)

  set(CMAKE_TOOLCHAIN_FILE
      "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain file" FORCE)

  set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Target triplet" FORCE)
  set(VCPKG_HOST_TRIPLET   "x64-windows" CACHE STRING "Host triplet"   FORCE)
else()
  message(FATAL_ERROR "Colony-Game currently only supports Windows.")
endif()
