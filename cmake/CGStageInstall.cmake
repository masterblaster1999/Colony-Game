# cmake/CGStageInstall.cmake
include_guard(GLOBAL)

# This staging logic is Windows-only (project is Windows-first)
if(NOT WIN32)
  return()
endif()

if(NOT TARGET ColonyGame)
  return()
endif()

# -------------- stage_win (portable staging folder) --------------
# Multi-config aware: keep Debug/Release outputs separate
set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage/$<CONFIG>")

set(_STAGE_COPY_RES_CMD "")
if(EXISTS "${CMAKE_SOURCE_DIR}/res")
  set(_STAGE_COPY_RES_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_SOURCE_DIR}/res" "${STAGE_DIR}/res")
endif()

# Prefer compiled shader outputs from build tree
set(_STAGE_COPY_SHADERS_CMD "")
if(EXISTS "${CMAKE_BINARY_DIR}/res/shaders")
  set(_STAGE_COPY_SHADERS_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/res/shaders" "${STAGE_DIR}/res/shaders")
elseif(EXISTS "${CMAKE_BINARY_DIR}/shaders")
  set(_STAGE_COPY_SHADERS_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders" "${STAGE_DIR}/shaders")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
  set(_STAGE_COPY_SHADERS_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_SOURCE_DIR}/shaders" "${STAGE_DIR}/shaders")
endif()

# Also copy renderer/Shaders runtime assets when present
set(_STAGE_COPY_RENDERER_SHADERS_CMD "")
if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders")
  set(_STAGE_COPY_RENDERER_SHADERS_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_SOURCE_DIR}/renderer/Shaders" "${STAGE_DIR}/renderer/Shaders")
endif()

# Optionally copy runtime-dependent DLLs (CMake 3.21+). This greatly reduces missing-DLL issues.
set(_STAGE_COPY_RUNTIME_DLLS_CMD "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
  set(_STAGE_COPY_RUNTIME_DLLS_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_RUNTIME_DLLS:ColonyGame> "${STAGE_DIR}/bin/")
endif()

# vcpkg runtime DLLs (best-effort fallback if TARGET_RUNTIME_DLLS isn't available or misses something)
set(_STAGE_COPY_VCPKG_RELEASE_CMD "")
set(_STAGE_COPY_VCPKG_DEBUG_CMD "")
if(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
  if(EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/bin")
    set(_STAGE_COPY_VCPKG_RELEASE_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/bin" "${STAGE_DIR}/bin")
  endif()
  if(EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/debug/bin")
    set(_STAGE_COPY_VCPKG_DEBUG_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/debug/bin" "${STAGE_DIR}/bin")
  endif()
elseif(DEFINED ENV{VCPKG_ROOT})
  if(EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/bin")
    set(_STAGE_COPY_VCPKG_RELEASE_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "$ENV{VCPKG_ROOT}/installed/x64-windows/bin" "${STAGE_DIR}/bin")
  endif()
  if(EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin")
    set(_STAGE_COPY_VCPKG_DEBUG_CMD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin" "${STAGE_DIR}/bin")
  endif()
endif()

add_custom_target(stage_win
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${STAGE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/bin"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/res"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/res/shaders"
  ${_STAGE_COPY_RES_CMD}
  ${_STAGE_COPY_SHADERS_CMD}
  ${_STAGE_COPY_RENDERER_SHADERS_CMD}
  # Copy the actual built EXE (fix for previous "$" placeholder)
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ColonyGame>" "${STAGE_DIR}/bin/"
  # Copy dependent runtime DLLs when supported (CMake 3.21+)
  ${_STAGE_COPY_RUNTIME_DLLS_CMD}
  # Fallback: copy vcpkg runtime bins (release+debug)
  ${_STAGE_COPY_VCPKG_RELEASE_CMD}
  ${_STAGE_COPY_VCPKG_DEBUG_CMD}
  VERBATIM
  COMMAND_EXPAND_LISTS
)
add_dependencies(stage_win ColonyGame)

# -------------- Install + CPack ZIP --------------
include(InstallRequiredSystemLibraries)  # bundles VC++ runtime when appropriate

# Keep exe at the root of the ZIP (as before)
install(TARGETS ColonyGame RUNTIME DESTINATION .)

if(EXISTS "${CMAKE_SOURCE_DIR}/res")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/res" DESTINATION .)
endif()

# Install compiled shaders if present; else copy sources to aid dev builds
if(EXISTS "${CMAKE_BINARY_DIR}/res/shaders")
  install(DIRECTORY "${CMAKE_BINARY_DIR}/res/shaders/" DESTINATION "shaders")
elseif(EXISTS "${CMAKE_BINARY_DIR}/shaders")
  install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "shaders")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/" DESTINATION "shaders")
endif()

# Keep renderer/Shaders alongside in case runtime loader expects it
if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/renderer/Shaders/" DESTINATION "renderer/Shaders")
endif()

set(CPACK_GENERATOR "ZIP")

# Robust Git hash detection for package name
set(_CG_GIT_HASH "")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _CG_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(NOT _CG_GIT_HASH)  # fallback
  set(_CG_GIT_HASH "unknown")
endif()

set(CPACK_PACKAGE_FILE_NAME "ColonyGame-${_CG_GIT_HASH}")
include(CPack)
