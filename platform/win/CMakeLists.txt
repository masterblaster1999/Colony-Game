# platform/win/CMakeLists.txt
# Windows platform library targets.
#
# colony_platform_win : owns DeviceResources.* (DX12 bootstrap) once
# win_platform        : Win32 window/entry + presentation helpers

cmake_minimum_required(VERSION 3.20)

set(PLATFORM_WIN_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(colony_append_existing OUT_VAR)
    foreach(path IN LISTS ARGN)
        if(EXISTS "${path}")
            list(APPEND ${OUT_VAR} "${path}")
        endif()
    endforeach()
    set(${OUT_VAR} "${${OUT_VAR}}" PARENT_SCOPE)
endfunction()

# --------------------------
# Per-source overrides
# --------------------------
# Disable PCH for this TU and apply /EHa only to this file.
set_source_files_properties("${PLATFORM_WIN_DIR}/win_entry.cpp"
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_OPTIONS "/EHa"
)

# --------------------------
# colony_platform_win
# --------------------------
# Best practice: compile DeviceResources.* once and link it wherever needed.
set(COLONY_DEVICE_SOURCES)
colony_append_existing(COLONY_DEVICE_SOURCES
    "${PLATFORM_WIN_DIR}/DeviceResources.cpp"
    "${PLATFORM_WIN_DIR}/DeviceResources.h"
)

# Determine whether we have at least one compilable translation unit.
set(_cg_has_device_cpp OFF)
foreach(_f IN LISTS COLONY_DEVICE_SOURCES)
    if(_f MATCHES "\\.(c|cc|cpp|cxx)$")
        set(_cg_has_device_cpp ON)
    endif()
endforeach()

if(_cg_has_device_cpp)
    add_library(colony_platform_win STATIC ${COLONY_DEVICE_SOURCES})
else()
    # IMPORTANT:
    # If we create a STATIC library with only headers, MSVC/VS can end up not emitting a .lib,
    # and dependents fail with LNK1104 trying to open colony_platform_win.lib.
    add_library(colony_platform_win INTERFACE)
    if(COLONY_DEVICE_SOURCES)
        target_sources(colony_platform_win INTERFACE ${COLONY_DEVICE_SOURCES})
    endif()
    message(STATUS "colony_platform_win: DeviceResources.cpp not found; created INTERFACE target.")
endif()

add_library(Colony::PlatformWin ALIAS colony_platform_win)
set_target_properties(colony_platform_win PROPERTIES FOLDER "libs")

get_target_property(_cg_plat_type colony_platform_win TYPE)
if(_cg_plat_type STREQUAL "INTERFACE_LIBRARY")
    set(_cg_plat_pub INTERFACE)
else()
    set(_cg_plat_pub PUBLIC)
endif()

target_compile_features(colony_platform_win ${_cg_plat_pub} cxx_std_20)
target_include_directories(colony_platform_win ${_cg_plat_pub}
    "${PLATFORM_WIN_DIR}"
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(colony_platform_win ${_cg_plat_pub}
    d3d12
    dxgi
    dxguid
)

if(MSVC)
    target_compile_definitions(colony_platform_win ${_cg_plat_pub}
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

unset(_cg_plat_type)
unset(_cg_plat_pub)
unset(_cg_has_device_cpp)

# --------------------------
# win_platform
# --------------------------
set(WIN_PLATFORM_SOURCES)
# We also include common alt filenames used in this repo:
colony_append_existing(WIN_PLATFORM_SOURCES
    "${PLATFORM_WIN_DIR}/win_entry.cpp"
    "${PLATFORM_WIN_DIR}/win_entry_win32.cpp"
    "${PLATFORM_WIN_DIR}/win_present_gdi.cpp"
    "${PLATFORM_WIN_DIR}/win_present_dxgi.cpp"
)

if(WIN_PLATFORM_SOURCES)
    add_library(win_platform STATIC ${WIN_PLATFORM_SOURCES})
    add_library(Colony::WinPlatform ALIAS win_platform)
    set_target_properties(win_platform PROPERTIES FOLDER "libs")

    target_compile_features(win_platform PUBLIC cxx_std_20)

    target_include_directories(win_platform PUBLIC
        "${PLATFORM_WIN_DIR}"
        "${CMAKE_SOURCE_DIR}"
    )

    target_link_libraries(win_platform PUBLIC
        user32
        gdi32
        shell32
        ole32
    )

    # Keep it lean for Windows headers
    target_compile_definitions(win_platform PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
else()
    message(WARNING "win_platform: no sources found; target not created.")
endif()
