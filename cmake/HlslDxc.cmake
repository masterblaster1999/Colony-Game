# cmake/HlslDxc.cmake
#
# Helper for compiling HLSL shaders with DXC into .cso files under
#   ${CMAKE_BINARY_DIR}/shaders
#
# Usage:
#   compile_hlsl(
#       OUT_VAR           # name of a CMake variable that will receive the .cso path
#       SRC               # path to .hlsl file (absolute or relative to CMAKE_SOURCE_DIR)
#       ENTRY             # HLSL entry function name (e.g. VSMain, PSMain, CSMain)
#       PROFILE           # DXC profile (vs_6_0, ps_6_0, cs_6_0, etc.)
#       [INCLUDE_DIRS ...]# optional list of include directories
#       [DEFINES ...]     # optional list of preprocessor defines (FOO=1 BAR)
#   )
#
#   The function:
#     - Ensures ${CMAKE_BINARY_DIR}/shaders exists
#     - Adds an add_custom_command() that runs dxc.exe
#     - Returns the .cso path in OUT_VAR
#
# You must have DXC on PATH or set DXC_EXECUTABLE before including this file.

if (NOT WIN32)
    message(FATAL_ERROR "HlslDxc.cmake is intended for Windows/DXC builds only.")
endif()

if (NOT DEFINED DXC_EXECUTABLE)
    # Try to find DXC on PATH or in common locations.
    find_program(DXC_EXECUTABLE
        NAMES dxc
        HINTS
            "$ENV{DXC_PATH}"
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/Bin32"
    )
    if (NOT DXC_EXECUTABLE)
        message(FATAL_ERROR "DXC (dxc.exe) not found. Set DXC_EXECUTABLE or ensure dxc is in PATH.")
    endif()
endif()

# compile_hlsl(OUT_VAR SRC ENTRY PROFILE [INCLUDE_DIRS ...] [DEFINES ...])
function(compile_hlsl OUT_VAR SRC ENTRY PROFILE)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs INCLUDE_DIRS DEFINES)
    cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Normalize source path
    if (IS_ABSOLUTE "${SRC}")
        set(SRC_ABS "${SRC}")
    else()
        set(SRC_ABS "${CMAKE_SOURCE_DIR}/${SRC}")
    endif()

    if (NOT EXISTS "${SRC_ABS}")
        message(FATAL_ERROR "compile_hlsl: Source file not found: ${SRC_ABS}")
    endif()

    get_filename_component(SRC_NAME_WE "${SRC_ABS}" NAME_WE)
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    set(OUTPUT_FILE "${OUTPUT_DIR}/${SRC_NAME_WE}.cso")

    # Build up DXC arguments
    set(DXC_ARGS)

    # Profile and entrypoint
    list(APPEND DXC_ARGS
        "/T" "${PROFILE}"
        "/E" "${ENTRY}"
    )

    # Optional defines
    foreach(def ${HLSL_DEFINES})
        list(APPEND DXC_ARGS "/D" "${def}")
    endforeach()

    # Optional include dirs
    foreach(inc ${HLSL_INCLUDE_DIRS})
        list(APPEND DXC_ARGS "/I" "${inc}")
    endforeach()

    # Output and input
    list(APPEND DXC_ARGS
        "/Fo" "${OUTPUT_FILE}"
        "${SRC_ABS}"
    )

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
        COMMAND "${DXC_EXECUTABLE}" ${DXC_ARGS}
        DEPENDS "${SRC_ABS}"
        COMMENT "HLSL (DXC): ${SRC_ABS} -> ${OUTPUT_FILE}"
        VERBATIM
    )

    # Return the output path
    set(${OUT_VAR} "${OUTPUT_FILE}" PARENT_SCOPE)
endfunction()
