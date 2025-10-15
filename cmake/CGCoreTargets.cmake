# cmake/CGCoreTargets.cmake
include_guard(GLOBAL)

# colony_core: aggregate non-UI/renderer engine sources if such dirs exist
set(COLONY_CORE_SOURCES "")
foreach(_maybe_dir IN ITEMS
  src/core src/engine src/common src/world src/worldgen src/pathfinding src/nav src/simulation)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_maybe_dir}")
    file(GLOB_RECURSE _g CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/${_maybe_dir}/*.cpp"
      "${CMAKE_SOURCE_DIR}/${_maybe_dir}/*.cxx"
      "${CMAKE_SOURCE_DIR}/${_maybe_dir}/*.c")
    list(APPEND COLONY_CORE_SOURCES ${_g})
  endif()
endforeach()
if(COLONY_CORE_SOURCES)
  list(FILTER COLONY_CORE_SOURCES EXCLUDE REGEX ".*/app/.*|.*/ui/.*|.*/imgui/.*|.*/renderer?/.*")
  add_library(colony_core STATIC ${COLONY_CORE_SOURCES})
  target_include_directories(colony_core PUBLIC "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
  target_link_libraries(colony_core PUBLIC colony_build_options)
  if(COLONY_USE_PCH)
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/common/pch_core.hpp")
      target_precompile_headers(colony_core PRIVATE src/common/pch_core.hpp)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch_core.hpp")
      target_precompile_headers(colony_core PRIVATE src/pch_core.hpp)
    endif()
  endif()
  set_property(TARGET colony_core PROPERTY FOLDER "libs")
  list(LENGTH COLONY_CORE_SOURCES _cnt)
  message(STATUS "colony_core: ${_cnt} sources")
endif()

# colony_platform_win: thin Win32 helpers if present
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win")
  file(GLOB PLATFORM_WIN_SRC CONFIGURE_DEPENDS
       "${CMAKE_SOURCE_DIR}/platform/win/*.cpp"
       "${CMAKE_SOURCE_DIR}/platform/win/*.cxx"
       "${CMAKE_SOURCE_DIR}/platform/win/*.c")
  if(PLATFORM_WIN_SRC)
    add_library(colony_platform_win STATIC ${PLATFORM_WIN_SRC})
    target_include_directories(colony_platform_win PUBLIC "${CMAKE_SOURCE_DIR}/platform/win")
    target_link_libraries(colony_platform_win PUBLIC shell32 ole32 colony_build_options)
    set_property(TARGET colony_platform_win PROPERTY FOLDER "libs")
  endif()
endif()
