# src/
# -----
# This directory is intended to define *executables only* (ColonyGame / optional tools).
# Do NOT glob every .cpp under src/ into ColonyGame; most code should live in libraries.

# NOTE: This project currently has multiple experimental/legacy entrypoints and tool mains
# living under src/. A recursive glob accidentally drags in:
#   - multiple WinMain / wWinMain definitions
#   - multiple GPU preference export objects
#   - prototype single-TU builds (src-gamesingletu.cpp, colonygame.cpp, etc.)
#   - tooling entrypoints (map viewer, etc.)
# which then breaks unity builds and/or causes duplicate symbols.


# Defensive: this CMakeLists.txt is meant to be included from the repo root.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Please configure from the repository root (top-level CMakeLists.txt), not from src/.")
endif()

set(_colony_game_sources
    # Windows entrypoint + thin app layer
    ${CMAKE_CURRENT_SOURCE_DIR}/app/EntryWinMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppWindow_Loop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppWindow_WndProc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AppWindow_Internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UserSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UserSettings.h

    # UI overlay (ImGui)
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImGuiLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ImGuiLayer.h

    # Input + temporary game module (keeps app layer thin)
    ${CMAKE_CURRENT_SOURCE_DIR}/input/InputEvent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/input/InputQueue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/input/InputBindingParse.h
    ${CMAKE_CURRENT_SOURCE_DIR}/input/InputMapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input/InputMapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/game/PrototypeGame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/game/PrototypeGame.h

    # Extracted modules (platform + loop) used by AppWindow
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/Win32Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/Win32Window.h
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/RawMouseInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/RawMouseInput.h
    ${CMAKE_CURRENT_SOURCE_DIR}/loop/DebugCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loop/DebugCamera.h
    ${CMAKE_CURRENT_SOURCE_DIR}/loop/FramePacer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loop/FramePacer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DxDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DxDevice.h
)

# Use WIN32 subsystem unless explicitly building with a console.
set(_win32_arg WIN32)
if(DEFINED SHOW_CONSOLE AND SHOW_CONSOLE)
    set(_win32_arg)
endif()

add_executable(ColonyGame ${_win32_arg})
target_sources(ColonyGame PRIVATE ${_colony_game_sources})

target_compile_features(ColonyGame PRIVATE cxx_std_23)

# The game ships with a tiny in-game debug overlay using ImGui.
target_compile_definitions(ColonyGame PRIVATE
    COLONY_WITH_IMGUI=1
)

# Keep include paths tight: we only need src/ for the thin app layer.
# (Library targets provide their own public include dirs.)
# Use BEFORE so these dirs are preferred in the include search order. 
target_include_directories(ColonyGame BEFORE PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
)

# Link against the modular libraries (engine/core/platform) + DirectX for the window layer.
target_link_libraries(ColonyGame PRIVATE
    colony::engine
    colony::platform_win
    imgui::imgui
    d3d11
    dxgi
    dxguid
    d3dcompiler
)

# Unity builds + internal-linkage helpers don't mix well across the codebase yet.
# Keep the executable as a normal multi-TU build regardless of global settings. 
set_target_properties(ColonyGame PROPERTIES UNITY_BUILD OFF)

if(MSVC)
    # Avoid MSBuild object file name collisions when sources share basenames.
    # (Still useful even with a curated list, and becomes critical once more files are added.)
    foreach(_src IN LISTS _colony_game_sources)
        if(_src MATCHES "\\.(c|cc|cxx|cpp)$")
            set_source_files_properties(${_src} PROPERTIES
                VS_SETTINGS "ObjectFileName=$(IntDir)%(Directory)%(Filename).obj")
        endif()
    endforeach()
endif()

# Project-wide helper hooks (defined in the root CMakeLists.txt)
colony_apply_msvc_runtime(ColonyGame)
colony_enable_warnings(ColonyGame)
colony_enable_pch(ColonyGame PRIVATE "${PROJECT_SOURCE_DIR}/src/pch.h")

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Optional: standalone launcher executable (kept separate so it doesn't collide with the game's WinMain)
if(DEFINED COLONY_BUILD_LAUNCHER AND COLONY_BUILD_LAUNCHER)
    set(_colony_launcher_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher/LauncherMain.cpp
    )

    add_executable(ColonyLauncher WIN32)
    target_sources(ColonyLauncher PRIVATE ${_colony_launcher_sources})
    target_compile_features(ColonyLauncher PRIVATE cxx_std_23)
    set_target_properties(ColonyLauncher PROPERTIES UNITY_BUILD OFF)

    if(MSVC)
        foreach(_src IN LISTS _colony_launcher_sources)
            if(_src MATCHES "\\.(c|cc|cxx|cpp)$")
                set_source_files_properties(${_src} PROPERTIES
                    VS_SETTINGS "ObjectFileName=$(IntDir)%(Directory)%(Filename).obj")
            endif()
        endforeach()
    endif()

    colony_apply_msvc_runtime(ColonyLauncher)
    colony_enable_warnings(ColonyLauncher)
endif()
