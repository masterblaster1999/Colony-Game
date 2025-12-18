# renderer/CMakeLists.txt
# Target: colony_renderer (colony::renderer)

set(_CG_RENDERER_DIRS
  "${CMAKE_SOURCE_DIR}/renderer"
  "${CMAKE_SOURCE_DIR}/src/renderer"
)

set(COLONY_RENDERER_SOURCES "")
foreach(_d IN LISTS _CG_RENDERER_DIRS)
  if(EXISTS "${_d}")
    file(GLOB_RECURSE _tmp CONFIGURE_DEPENDS
      "${_d}/*.c"
      "${_d}/*.cpp"
    )
    list(APPEND COLONY_RENDERER_SOURCES ${_tmp})
  endif()
endforeach()
list(REMOVE_DUPLICATES COLONY_RENDERER_SOURCES)

if(COLONY_RENDERER_SOURCES)
  add_library(colony_renderer STATIC)
  target_sources(colony_renderer PRIVATE ${COLONY_RENDERER_SOURCES})
else()
  add_library(colony_renderer INTERFACE)
  message(STATUS "colony_renderer: no sources found; created INTERFACE target.")
endif()

add_library(colony::renderer ALIAS colony_renderer)
set_target_properties(colony_renderer PROPERTIES FOLDER "libs")

get_target_property(_cg_r_type colony_renderer TYPE)
if(_cg_r_type STREQUAL "INTERFACE_LIBRARY")
  set(_cg_r_pub INTERFACE)
else()
  set(_cg_r_pub PUBLIC)
endif()

target_compile_features(colony_renderer ${_cg_r_pub} cxx_std_23)

target_include_directories(colony_renderer ${_cg_r_pub}
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/renderer>"
  "$<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>"
)

# Dependencies:
target_link_libraries(colony_renderer ${_cg_r_pub}
  colony::core
)

# SDL2 + ImGui usually belong to renderer; if any other module needs them,
# it should link them too (or rely on renderer's PUBLIC link). :contentReference[oaicite:7]{index=7}
if(TARGET SDL2::SDL2)
  target_link_libraries(colony_renderer ${_cg_r_pub} SDL2::SDL2)
endif()
if(TARGET imgui::imgui)
  target_link_libraries(colony_renderer ${_cg_r_pub} imgui::imgui)
endif()

if(COLONY_USE_PCH AND NOT _cg_r_type STREQUAL "INTERFACE_LIBRARY")
  target_precompile_headers(colony_renderer PRIVATE "${COLONY_PCH_HEADER}")
endif()

if(NOT _cg_r_type STREQUAL "INTERFACE_LIBRARY")
  colony_apply_msvc_runtime(colony_renderer)
  colony_apply_debug_runtime_checks(colony_renderer)
endif()

unset(_cg_r_type)
unset(_cg_r_pub)

