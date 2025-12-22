# src/CMakeLists.txt
# Builds the actual game executable(s).

set(COLONY_GAME_SOURCES "")

if(EXISTS "${CMAKE_SOURCE_DIR}/src")
    file(GLOB_RECURSE COLONY_GAME_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
    )
endif()

list(REMOVE_DUPLICATES COLONY_GAME_SOURCES)

if(COLONY_GAME_SOURCES)
    add_executable(ColonyGame WIN32 ${COLONY_GAME_SOURCES})
    set_target_properties(ColonyGame PROPERTIES FOLDER "apps")

    target_compile_features(ColonyGame PRIVATE cxx_std_23)

    target_include_directories(ColonyGame PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )

    target_link_libraries(ColonyGame PRIVATE
        colony::engine
        colony::platform_win
    )

    # Shaders: copy compiled shaders next to the executable, and link the shaders target if present.
    if(TARGET ColonyShaders)
        add_dependencies(ColonyGame ColonyShaders)
        add_custom_command(TARGET ColonyGame POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/shaders"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:ColonyGame>/shaders"
            COMMENT "Copying compiled shaders next to ColonyGame"
        )
    endif()

    colony_enable_pch(ColonyGame PRIVATE "${COLONY_PCH_HEADER}")
    colony_apply_msvc_runtime(ColonyGame)
    colony_apply_debug_runtime_checks(ColonyGame)
    colony_apply_seh_for_crashdump(ColonyGame)

    # MSBuild warning MSB8027 (two files with same basename): put object files in unique subfolders.
    # IMPORTANT: Use %(Directory) (rootless) instead of %(RelativeDir) to avoid MSB3191 paths like "...\\Debug\\D:\\...".
    if(MSVC)
        foreach(_src IN LISTS COLONY_GAME_SOURCES)
            if(_src MATCHES ".*\\.(c|cc|cxx|cpp)$")
                set_source_files_properties("${_src}" PROPERTIES
                    VS_SETTINGS "ObjectFileName=$(IntDir)%(Directory)"
                )
            endif()
        endforeach()
    endif()

else()
    message(WARNING "No sources found under src/; ColonyGame target not created.")
endif()

# Optional: map_viewer tool (Windows)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/tools/map_viewer")
    set(COLONY_MAP_VIEWER_SOURCES "")
    file(GLOB_RECURSE COLONY_MAP_VIEWER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/tools/map_viewer/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/tools/map_viewer/*.hpp"
    )
    list(REMOVE_DUPLICATES COLONY_MAP_VIEWER_SOURCES)

    if(COLONY_MAP_VIEWER_SOURCES)
        add_executable(ColonyMapViewer WIN32 ${COLONY_MAP_VIEWER_SOURCES})
        set_target_properties(ColonyMapViewer PROPERTIES FOLDER "tools")

        target_compile_features(ColonyMapViewer PRIVATE cxx_std_23)

        target_include_directories(ColonyMapViewer PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
        )

        target_link_libraries(ColonyMapViewer PRIVATE
            colony::engine
            colony::platform_win
        )

        colony_enable_pch(ColonyMapViewer PRIVATE "${COLONY_PCH_HEADER}")
        colony_apply_msvc_runtime(ColonyMapViewer)
        colony_apply_debug_runtime_checks(ColonyMapViewer)
        colony_apply_seh_for_crashdump(ColonyMapViewer)

        if(MSVC)
            foreach(_src IN LISTS COLONY_MAP_VIEWER_SOURCES)
                if(_src MATCHES ".*\\.(c|cc|cxx|cpp)$")
                    set_source_files_properties("${_src}" PROPERTIES
                        VS_SETTINGS "ObjectFileName=$(IntDir)%(Directory)"
                    )
                endif()
            endforeach()
        endif()
    endif()
endif()
