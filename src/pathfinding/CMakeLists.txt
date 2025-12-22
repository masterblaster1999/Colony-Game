# pathfinding/CMakeLists.txt
# Target: colony_nav (colony::nav)
# Sources come from pathfinding/ and hpa/ (keeps your current layout).

set(COLONY_NAV_SOURCES "")

if(EXISTS "${CMAKE_SOURCE_DIR}/pathfinding")
    file(GLOB_RECURSE _pf CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/pathfinding/*.c"
        "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
    )
    list(APPEND COLONY_NAV_SOURCES ${_pf})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/hpa")
    file(GLOB_RECURSE _hpa CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/hpa/*.c"
        "${CMAKE_SOURCE_DIR}/hpa/*.cpp"
    )
    list(APPEND COLONY_NAV_SOURCES ${_hpa})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/src/nav")
    file(GLOB_RECURSE _nav CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/nav/*.c"
        "${CMAKE_SOURCE_DIR}/src/nav/*.cpp"
    )
    list(APPEND COLONY_NAV_SOURCES ${_nav})
endif()

list(REMOVE_DUPLICATES COLONY_NAV_SOURCES)

if(COLONY_NAV_SOURCES)
    add_library(colony_nav STATIC ${COLONY_NAV_SOURCES})
else()
    add_library(colony_nav INTERFACE)
    message(STATUS "colony_nav: no sources found; created INTERFACE target.")
endif()

add_library(colony::nav  ALIAS colony_nav)
add_library(colony::path ALIAS colony_nav)

set_target_properties(colony_nav PROPERTIES FOLDER "libs")

get_target_property(_cg_nav_type colony_nav TYPE)
if(_cg_nav_type STREQUAL "INTERFACE_LIBRARY")
    set(_cg_nav_pub INTERFACE)
else()
    set(_cg_nav_pub PUBLIC)
endif()

target_compile_features(colony_nav ${_cg_nav_pub} cxx_std_23)

target_include_directories(colony_nav ${_cg_nav_pub}
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/pathfinding>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/hpa>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/nav>
    $<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>
)

target_link_libraries(colony_nav ${_cg_nav_pub} colony::core)

if(COLONY_USE_PCH AND NOT _cg_nav_type STREQUAL "INTERFACE_LIBRARY")
    if(IS_ABSOLUTE "${COLONY_PCH_HEADER}")
        target_precompile_headers(colony_nav PRIVATE "${COLONY_PCH_HEADER}")
    else()
        target_precompile_headers(colony_nav PRIVATE "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
    endif()
endif()

if(NOT _cg_nav_type STREQUAL "INTERFACE_LIBRARY")
    colony_apply_msvc_runtime(colony_nav)
    colony_apply_debug_runtime_checks(colony_nav)
endif()

# MSBuild warning MSB8027 (two files with same basename): put object files in unique subfolders.
# IMPORTANT: Use %(Directory) (rootless) instead of %(RelativeDir) to avoid MSB3191 paths like "...\\Debug\\D:\\...".
if(MSVC AND NOT _cg_nav_type STREQUAL "INTERFACE_LIBRARY")
    foreach(_src IN LISTS COLONY_NAV_SOURCES)
        if(_src MATCHES ".*\\.(c|cc|cxx|cpp)$")
            set_source_files_properties("${_src}" PROPERTIES
                VS_SETTINGS "ObjectFileName=$(IntDir)%(Directory)"
            )
        endif()
    endforeach()
endif()

unset(_cg_nav_type)
unset(_cg_nav_pub)
