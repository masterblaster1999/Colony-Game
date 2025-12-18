# engine/CMakeLists.txt
# Engine module (STATIC library)

# Gather sources (kept simple + robust; you can replace with explicit lists later)
file(GLOB_RECURSE COLONY_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if(NOT COLONY_ENGINE_SOURCES)
  message(FATAL_ERROR
    "engine/: No .c/.cpp sources were found. "
    "Either add sources, or adjust the glob patterns in engine/CMakeLists.txt.")
endif()

add_library(colony_engine STATIC ${COLONY_ENGINE_SOURCES})
add_library(colony::engine ALIAS colony_engine)

target_compile_features(colony_engine PUBLIC cxx_std_23)

# Public headers are typically shared from /include + repo root
target_include_directories(colony_engine
  PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}"
)

# Engine usually depends on core + renderer + nav (if those targets exist)
if(TARGET colony_core)
  target_link_libraries(colony_engine PUBLIC colony_core)
endif()

if(TARGET colony_renderer)
  target_link_libraries(colony_engine PUBLIC colony_renderer)
endif()

# If you renamed pathfinding to "nav", support either.
if(TARGET colony_nav)
  target_link_libraries(colony_engine PUBLIC colony_nav)
elseif(TARGET colony_path)
  target_link_libraries(colony_engine PUBLIC colony_path)
endif()

# Platform lib is fine to link transitively (often already via core),
# but this keeps engine standalone if you ever link it directly.
if(TARGET colony_platform_win)
  target_link_libraries(colony_engine PUBLIC colony_platform_win)
endif()

# PCH (uses your root variables)
if(DEFINED COLONY_USE_PCH AND COLONY_USE_PCH)
  if(DEFINED COLONY_PCH_HEADER AND EXISTS "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
    target_precompile_headers(colony_engine PRIVATE "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  endif()
endif()

# Apply MSVC runtime + Debug checks if helper functions exist (defined in root)
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_engine)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_engine)
endif()

# Visual Studio folder organization
set_target_properties(colony_engine PROPERTIES FOLDER "colony/engine")
