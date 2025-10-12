# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # genex in OUTPUT requires 3.20+; VS HLSL props are 3.1+/3.2+

# -------- Discover sources (exclude vendored imgui if present) --------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.h"   "${CMAKE_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)
list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

# Remove legacy/duplicate entry points
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/main\\.cpp$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/app/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/render/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/terrain/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/core/.*\\.(c|cc|cxx|cpp)$")

# --- BEGIN: Enable crash-dump TU with per-file /EHa (fixes MSVC C2712) ---
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpStub\\.cpp$")
  if(MSVC)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
      PROPERTIES COMPILE_OPTIONS "/EHa")
  endif()
endif()
# --- END: Enable crash-dump TU with per-file /EHa ---

# --- Frontend gating: native Win32 (default) or SDL ---
if(FRONTEND STREQUAL "sdl")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
else()
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
endif()

add_executable(ColonyGame ${SRC_ALL})

# ---------------------------------------------------------------------------
# POST-BUILD: stage runtime assets next to the ColonyGame.exe
# This must live in the SAME CMake directory where ColonyGame is defined.
# CMake docs: the <target> in add_custom_command(TARGET ...) must be defined
# in the current directory; targets in other directories cannot be specified.
# ---------------------------------------------------------------------------
add_custom_command(TARGET ColonyGame POST_BUILD
  # Ensure output folders exist
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:ColonyGame>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:ColonyGame>/res"
  # Copy static assets shipped from the repo
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ColonyGame>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/res"    "$<TARGET_FILE_DIR:ColonyGame>/res"
  # (Optional) If your shader pipeline emits compiled outputs into build/shaders,
  # uncomment this block. Keep it commented if that folder may not exist.
  # COMMAND ${CMAKE_COMMAND} -E make_directory
  #         "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  # COMMAND ${CMAKE_COMMAND} -E copy_directory
  #         "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>" "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  VERBATIM
)

# ------------------------- SHADERS: choose ONE route per generator -------------------------
# A) Visual Studio generator -> let MSBuild compile .hlsl (VS_SHADER_* props)
# B) Other generators (e.g., Ninja) -> use shaders.json + colony_register_shaders()

# A) Visual Studio path (MSBuild HLSL)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(HLSL_DIR  "${CMAKE_SOURCE_DIR}/renderer/Shaders")
  set(HLSL_INC_PRIMARY  "${HLSL_DIR}/include")
  set(HLSL_INC_ALT      "${CMAKE_SOURCE_DIR}/shaders/include")  # optional secondary include root
  set(_VS_OUTDIR "$(OutDir)res\\shaders")  # place .cso where the runtime expects them

  # --- Only compile entrypoint-bearing shaders; treat others as header-only ---
  # 1) Explicitly collect only compileable HLSL with *_vs/_ps/_cs suffixes
  file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
    "${HLSL_DIR}/*_vs.hlsl"
    "${HLSL_DIR}/*_ps.hlsl"
    "${HLSL_DIR}/*_cs.hlsl"
  )

  # 2) Library/utility .hlsl (no entry point): mark header-only
  file(GLOB_RECURSE HLSL_ALL_HLSL CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsl")
  set(HLSL_LIBRARY "")
  foreach(_f IN LISTS HLSL_ALL_HLSL)
    list(FIND HLSL_SOURCES "${_f}" _idx)
    if(_idx EQUAL -1)
      list(APPEND HLSL_LIBRARY "${_f}")
    endif()
  endforeach()

  # 3) .hlsli files are includes, not compiled
  file(GLOB_RECURSE HLSL_INCLUDES CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsli")

  # Add to target so they appear in Solution Explorer
  if(HLSL_SOURCES)
    target_sources(ColonyGame PRIVATE ${HLSL_SOURCES})
  endif()
  if(HLSL_LIBRARY)
    target_sources(ColonyGame PRIVATE ${HLSL_LIBRARY})
    set_source_files_properties(${HLSL_LIBRARY} PROPERTIES HEADER_FILE_ONLY ON)
  endif()
  if(HLSL_INCLUDES)
    target_sources(ColonyGame PRIVATE ${HLSL_INCLUDES})
    set_source_files_properties(${HLSL_INCLUDES} PROPERTIES HEADER_FILE_ONLY ON)
  endif()

  # Group them in the IDE
  source_group("Shaders\\Sources"  FILES ${HLSL_SOURCES})
  source_group("Shaders\\Library"  FILES ${HLSL_LIBRARY})
  source_group("Shaders\\Includes" FILES ${HLSL_INCLUDES})

  # Construct include flags for FXC (support both include roots if they exist)
  set(_HLSL_INC_FLAGS "")
  if(EXISTS "${HLSL_INC_PRIMARY}")
    string(APPEND _HLSL_INC_FLAGS " /I\"${HLSL_INC_PRIMARY}\"")
  endif()
  if(EXISTS "${HLSL_INC_ALT}")
    string(APPEND _HLSL_INC_FLAGS " /I\"${HLSL_INC_ALT}\"")
  endif()

  # Configure VS shader properties by suffix: *_vs.hlsl / *_ps.hlsl / *_cs.hlsl
  function(_configure_vs_shader _file)
    get_filename_component(_we "${_file}" NAME_WE)
    set(_type "")
    if(_we MATCHES "_vs$")      set(_type "Vertex")
    elseif(_we MATCHES "_ps$")  set(_type "Pixel")
    elseif(_we MATCHES "_cs$")  set(_type "Compute")
    else()                      return() # Only compile files in HLSL_SOURCES
    endif()

    set_source_files_properties("${_file}" PROPERTIES
      VS_SHADER_TYPE              "${_type}"
      VS_SHADER_MODEL             "5.0"
      VS_SHADER_ENTRYPOINT        "main"
      VS_SHADER_OBJECT_FILE_NAME  "${_VS_OUTDIR}\\%(Filename).cso"
      VS_SHADER_FLAGS             "$<$<CONFIG:Debug>:/Od;/Zi>${_HLSL_INC_FLAGS}"
    )
  endfunction()

  foreach(_sh IN LISTS HLSL_SOURCES)
    _configure_vs_shader("${_sh}")
  endforeach()

else()
  # B) Manifest + DXC/FXC path (single-config generators)
  include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake OPTIONAL RESULT_VARIABLE _colony_shaders_included)

  # Detect a shaders.json manifest in either renderer/Shaders or shaders
  set(_COLONY_MANIFEST "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
  endif()

  if(_COLONY_MANIFEST AND COMMAND colony_register_shaders)
    set(_COLONY_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
    colony_register_shaders(
      TARGET       ColonyGame
      MANIFEST     "${_COLONY_MANIFEST}"
      OUTPUT_DIR   "${_COLONY_SHADER_OUT}"
      INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/renderer/Shaders"
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/include"
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/include"
      DXC_ARGS     -nologo
    )

    # Optional install step for 'cmake --install'
    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)
    endif()

    # Stage compiled shader blobs (manifest output) next to the EXE after build.
    add_custom_command(TARGET ColonyGame POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
              "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
      VERBATIM
    )
  elseif(_COLONY_MANIFEST)
    message(WARNING
      "Found '${_COLONY_MANIFEST}', but 'colony_register_shaders()' is not defined. "
      "Ensure cmake/ColonyShaders.cmake provides it, or build with a Visual Studio generator to let MSBuild compile HLSL."
    )
  else()
    message(STATUS "No shaders.json manifest found; DXC shader registration skipped.")
  endif()
endif()
# -------------------------------------------------------------------------------------------

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

target_compile_features(ColonyGame PRIVATE cxx_std_20)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)

# Make sure this target is /utf-8 too (fmt 11.x requires it)
if(MSVC)
  target_compile_options(ColonyGame PRIVATE /utf-8)
endif()

# Precompiled header (optional, gated by COLONY_USE_PCH)
if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# Unity builds (opt‑in)
if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

# Compile-time defines for optional systems + deterministic frontend switch
target_compile_definitions(ColonyGame PRIVATE
  $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

# Link libs (propagate Windows flags/defs from interface target)
target_link_libraries(ColonyGame
  PRIVATE
    colony_build_options
    $<$<STREQUAL:${FRONTEND},sdl>:$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>>
    $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2>
)

# Add Windows platform resources and helpers (manifest, version info, crash handler, GPU preference)
if(WIN32)
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()
  foreach(_f IN ITEMS
      "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp"
      "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp"
    )
    if(EXISTS "${_f}")
      target_sources(ColonyGame PRIVATE "${_f}")
    endif()
  endforeach()
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Make the game the startup project in Visual Studio
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Output directories per configuration (for multi-config generators like VS)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
endif()

# ---- Optional: copy shared shader header for modding/debugging ----
if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli")
  set(COLONY_SHADER_INC_SRC "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli")
  set(COLONY_SHADER_INC_DST "$<TARGET_FILE_DIR:ColonyGame>/res/shaders/include/common.hlsli")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res/shaders/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${COLONY_SHADER_INC_SRC}" "${COLONY_SHADER_INC_DST}"
    VERBATIM
  )
endif()

# ---------------- Quality/security toggles you can enable upstream ----------------
# Example: MSVC AddressSanitizer in Debug builds:
#   target_compile_options(ColonyGame PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
#   target_link_options(ColonyGame    PRIVATE $<$<CONFIG:Debug>:/INFERASANLIBS>)
# Example: Control Flow Guard in Release builds:
#   target_compile_options(ColonyGame PRIVATE $<$<CONFIG:Release>:/guard:cf>)
#   target_link_options(ColonyGame    PRIVATE $<$<CONFIG:Release>:/guard:cf>)
# ------------------------------------------------------------------------------  End of file
