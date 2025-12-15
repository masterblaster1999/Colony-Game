# src/CMakeLists.txt

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)
  project(ColonyGame LANGUAGES CXX)
endif()

get_filename_component(COLONY_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# ------------------------------ Options ------------------------------
include("${COLONY_ROOT_DIR}/cmake/CGOptions.cmake")

# ------------------------------ Modules ------------------------------
include("${COLONY_ROOT_DIR}/cmake/CGSources.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGCoreTarget.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGExecutables.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGShadersPipeline.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGRuntimeLayout.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGInstall.cmake")

# ------------------------------ Core sources (ownership rules) ------------------------------
cg_collect_core_sources(COLONY_CORE_SOURCES
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
)

# ------------------------------ Core target (one-liner) ------------------------------
cg_setup_core_target(
  TARGET colony_core
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
  SOURCES ${COLONY_CORE_SOURCES}
  UNITY_BUILD ${COLONY_UNITY_BUILD}
  USE_PCH ${COLONY_USE_PCH}
  PCH_HEADER "${COLONY_PCH_HEADER}"
  ENABLE_IMGUI ${ENABLE_IMGUI}
  ENABLE_TRACY ${ENABLE_TRACY}
  WERROR ${COLONY_WERROR}
)

# ------------------------------ Executables (one-liner) ------------------------------
cg_setup_executables(
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
  SHOW_CONSOLE ${SHOW_CONSOLE}
  UNITY_BUILD ${COLONY_UNITY_BUILD}
  BUILD_LAUNCHER ${COLONY_BUILD_LAUNCHER}
  CORE_TARGET colony_core
  BUILD_OPTIONS_TARGET colony_build_options
  OUT_TARGETS COLONY_EXECUTABLE_TARGETS
)

# ------------------------------ Runtime layout (one-liners) ------------------------------
cg_configure_runtime_output(
  TARGETS ${COLONY_EXECUTABLE_TARGETS}
  BASE_DIR "${CMAKE_BINARY_DIR}/bin"
)

# Copy assets/res/Content next to the EXE after build
cg_add_post_build_content_copy(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
)

# ------------------------------ Shaders (one-liner) ------------------------------
cg_setup_shaders_pipeline(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
  ENABLE_COMPUTE_SHADERS ${COLONY_ENABLE_COMPUTE_SHADERS}
)

# ------------------------------ Install (one-liner) ------------------------------
cg_install_colony_artifacts(
  ROOT_DIR "${COLONY_ROOT_DIR}"
  GAME_TARGET ColonyGame
  LAUNCHER_TARGET ColonyLauncher
  RUNTIME_DESTINATION "."
)
