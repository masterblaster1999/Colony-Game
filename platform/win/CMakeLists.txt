# platform/win/CMakeLists.txt
#
# Windows-only platform modules:
#   - colony_platform_win : owns DeviceResources.* (DX12 bootstrap) once
#   - win_platform        : windowing/OS utilities (CrashHandler, WinApp, etc.)
#
# NOTE: This file is robust to your current repo contents — it only adds files
#       that actually exist in platform/win to avoid build breaks.

cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
    message(FATAL_ERROR "This submodule builds Windows-only.")
endif()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
set(PLATFORM_WIN_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Append only the files that actually exist, so the build doesn't fail if some
# of the optional sources aren't present in your repo yet.
function(colony_append_existing OUT_VAR)
    set(_acc "${${OUT_VAR}}")
    foreach(_f IN LISTS ARGN)
        if(EXISTS "${_f}")
            list(APPEND _acc "${_f}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _acc)
    set(${OUT_VAR} "${_acc}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Per-source overrides
# ---------------------------------------------------------------------------
# Best practice: CrashDumpWin.cpp is typically built with /EHa (SEH-aware),
# which conflicts with a PCH built with /EHsc. Disable PCH for this TU and
# apply /EHa only to this file.
if(MSVC)
    set(_CRASHDUMP_WIN "${PLATFORM_WIN_DIR}/CrashDumpWin.cpp")
    if(EXISTS "${_CRASHDUMP_WIN}")
        set_source_files_properties("${_CRASHDUMP_WIN}" PROPERTIES
            SKIP_PRECOMPILE_HEADERS ON
            SKIP_UNITY_BUILD_INCLUSION ON
            COMPILE_OPTIONS "/EHa"
        )
    endif()
endif()

# Keep Unity builds enabled for win_platform overall, but compile these
# "touchy" translation units separately to avoid unity/ODR redefinition issues
# when they get bucketed into the same unity TU.
set(_WIN_PLATFORM_UNITY_EXCLUDE
    "${PLATFORM_WIN_DIR}/win_entry.cpp"
    "${PLATFORM_WIN_DIR}/win_present_gdi.cpp"
)

foreach(_src IN LISTS _WIN_PLATFORM_UNITY_EXCLUDE)
    if(EXISTS "${_src}")
        set_source_files_properties("${_src}" PROPERTIES
            SKIP_UNITY_BUILD_INCLUSION ON
        )
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Library: colony_platform_win  (DX12 DeviceResources owner)
# Compile DeviceResources.* exactly once here; link this lib ONCE in the EXE.
# ---------------------------------------------------------------------------
set(COLONY_DEVICE_SOURCES)
colony_append_existing(COLONY_DEVICE_SOURCES
    "${PLATFORM_WIN_DIR}/DeviceResources.cpp"
    "${PLATFORM_WIN_DIR}/DeviceResources.h"
)

if(COLONY_DEVICE_SOURCES)
    add_library(colony_platform_win STATIC ${COLONY_DEVICE_SOURCES})
    target_compile_features(colony_platform_win PUBLIC cxx_std_20)
    target_include_directories(colony_platform_win PUBLIC
        "${PLATFORM_WIN_DIR}"
        "${CMAKE_SOURCE_DIR}"
    )
    # D3D12 + DXGI + dxguid (IID constants)
    target_link_libraries(colony_platform_win PUBLIC
        d3d12
        dxgi
        dxguid
    )
    add_library(Colony::PlatformWin ALIAS colony_platform_win)
endif()

# ---------------------------------------------------------------------------
# Library: win_platform  (Windowing/OS, CrashHandler, Filesystem, etc.)
# This mirrors your requested snippet. We also include common alt filenames
# used in this repo (e.g., CrashHandlerWin.* / Window.*) and compile them if
# present. Headers in the list are fine — CMake tolerates them in sources.
# ---------------------------------------------------------------------------
set(WIN_PLATFORM_SOURCES)
colony_append_existing(WIN_PLATFORM_SOURCES
    # From your snippet (add if present):
    "${PLATFORM_WIN_DIR}/win_entry.cpp"
    "${PLATFORM_WIN_DIR}/win_present_gdi.cpp"
    "${PLATFORM_WIN_DIR}/WinApp.cpp"
    "${PLATFORM_WIN_DIR}/CrashHandler.cpp"
    "${PLATFORM_WIN_DIR}/FilesystemWin.cpp"

    # Common alternates already used in the repo:
    "${PLATFORM_WIN_DIR}/CrashHandlerWin.cpp"
    "${PLATFORM_WIN_DIR}/CrashHandlerWin.h"
    "${PLATFORM_WIN_DIR}/Window.cpp"
    "${PLATFORM_WIN_DIR}/Window.h"
)

if(WIN_PLATFORM_SOURCES)
    add_library(win_platform STATIC ${WIN_PLATFORM_SOURCES})

    # Public include path for the module
    target_include_directories(win_platform PUBLIC "${PLATFORM_WIN_DIR}")

    if(MSVC)
        # Link Win32 system libs used by the module
        target_link_libraries(win_platform PUBLIC
            Shcore         # SetProcessDpiAwareness (fallback DPI route)
            Shell32        # Known folders, shell helpers
            Dbghelp        # MiniDumpWriteDump
            Dwmapi         # DwmFlush / composition queries
            Gdi32          # StretchDIBits / GDI present path
            User32         # Windowing, messages, DPI contexts
            Xinput9_1_0    # XInput (light baseline)
            Winmm          # timeBeginPeriod / timeEndPeriod
        )
        target_compile_definitions(win_platform PUBLIC
            UNICODE _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
        target_compile_features(win_platform PUBLIC cxx_std_20)
    endif()

    add_library(Colony::WinPlatform ALIAS win_platform)
endif()

# ---------------------------------------------------------------------------
# Usage (link these in your EXE target; don't link colony_platform_win into
# intermediate static libs to avoid duplicate objects at the final link):
#
# if(TARGET ColonyGame)
#   target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin Colony::WinPlatform)
# elseif(TARGET colony_game)
#   target_link_libraries(colony_game PRIVATE Colony::PlatformWin Colony::WinPlatform)
# endif()
# ---------------------------------------------------------------------------
