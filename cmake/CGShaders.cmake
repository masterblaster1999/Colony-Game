# cmake/CGShaders.cmake
include_guard(GLOBAL)
include(CMakeParseArguments)

# Use modern, predictable variable/escape evaluation for $ENV{...}
# (Recommended in CMake docs; helps with env names that include special chars.)
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# -----------------------------------------------------------------------------
# Internal: find fxc.exe from the Windows SDK (WindowsSdkDir) with fallbacks.
# Avoids $ENV{ProgramFiles(x86)} entirely to prevent parsing issues.
# -----------------------------------------------------------------------------
function(_cg_find_fxc OUT_EXE)
  if(NOT WIN32)
    message(FATAL_ERROR "cg_compile_hlsl is Windows-only; renderer is D3D11.")
  endif()

  # Allow an explicit override in CI or developer machines:
  # cmake -DCOLONY_FXC_PATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe"
  set(COLONY_FXC_PATH "${COLONY_FXC_PATH}" CACHE FILEPATH "Optional: full path to fxc.exe")
  if(COLONY_FXC_PATH AND EXISTS "${COLONY_FXC_PATH}")
    file(TO_CMAKE_PATH "${COLONY_FXC_PATH}" _fxc_path_norm)
    set(${OUT_EXE} "${_fxc_path_norm}" PARENT_SCOPE)
    return()
  endif()

  # Hints from Windows SDK root. This is the supported way to find SDK layout.
  set(_HINTS "")
  if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    list(APPEND _HINTS
      "$ENV{WindowsSdkDir}/bin"
      "$ENV{WindowsSdkDir}/bin/x64"
      "$ENV{WindowsSdkDir}/bin/arm64"
    )
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _HINTS
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/arm64"
      )
    else()
      # Non‑VS generators (e.g., Ninja): probe all versioned bin folders and prefer newest
      file(GLOB _sdk_bin_vers LIST_DIRECTORIES TRUE "$ENV{WindowsSdkDir}/bin/*")
      if(_sdk_bin_vers)
        list(SORT _sdk_bin_vers ORDER DESCENDING)
        foreach(_v IN LISTS _sdk_bin_vers)
          list(APPEND _HINTS "${_v}" "${_v}/x64" "${_v}/arm64")
        endforeach()
      endif()
    endif()
  endif()

  # Well-known absolute fallbacks (quoted, using forward slashes)
  list(APPEND _HINTS
    "C:/Program Files (x86)/Windows Kits/11/bin"
    "C:/Program Files (x86)/Windows Kits/11/bin/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin"
    "C:/Program Files (x86)/Windows Kits/10/bin/x64"
  )
  list(REMOVE_DUPLICATES _HINTS)

  # Try only our hints first (to avoid random PATH surprises)
  find_program(_FXC_EXE NAMES fxc fxc.exe
    HINTS ${_HINTS}
    PATHS ${_HINTS}
    PATH_SUFFIXES x64 x86
    NO_DEFAULT_PATH
  )

  # Last resort: PATH
  if(NOT _FXC_EXE)
    find_program(_FXC_EXE NAMES fxc fxc.exe)
  endif()

  if(NOT _FXC_EXE)
    message(FATAL_ERROR
      "fxc.exe not found. Install the Windows 10/11 SDK (HLSL FXC tool) "
      "or pass -DCOLONY_FXC_PATH=... to cmake.")
  endif()

  file(TO_CMAKE_PATH "${_FXC_EXE}" _FXC_EXE_NORM)
  set(${OUT_EXE} "${_FXC_EXE_NORM}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# Internal: infer a reasonable SM5 profile from filename suffix (_vs/_ps/_cs/..)
# -----------------------------------------------------------------------------
function(_cg_guess_profile_from_name SRC OUT_PROFILE)
  get_filename_component(_name_we "${SRC}" NAME_WE)
  set(_p "ps_5_0")
  if(_name_we MATCHES "_vs$")   set(_p "vs_5_0") endif()
  if(_name_we MATCHES "_ps$")   set(_p "ps_5_0") endif()
  if(_name_we MATCHES "_cs$")   set(_p "cs_5_0") endif()
  if(_name_we MATCHES "_gs$")   set(_p "gs_5_0") endif()
  if(_name_we MATCHES "_hs$")   set(_p "hs_5_0") endif()
  if(_name_we MATCHES "_ds$")   set(_p "ds_5_0") endif()
  set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# Public API: compile HLSL with FXC into .cso blobs (SM 5.x for D3D11)
# cg_compile_hlsl(
#   <TARGET_NAME>
#   SHADERS <list of .hlsl files>
#   [INCLUDE_DIRS <dirs...>]
#   [DEFINES <defs...>]
#   [OUTPUT_DIR <dir>]          # defaults to ${CMAKE_BINARY_DIR}/shaders
# )
# -----------------------------------------------------------------------------
function(cg_compile_hlsl TARGET_NAME)
  if(NOT WIN32)
    message(FATAL_ERROR "cg_compile_hlsl is Windows-only (D3D11).")
  endif()

  set(_opts)
  set(_one SHADERS OUTPUT_DIR)
  set(_many INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CG_SHADERS)
    message(WARNING "cg_compile_hlsl: no SHADERS specified")
    add_custom_target(${TARGET_NAME})
    return()
  endif()

  _cg_find_fxc(FXC_EXE)
  if(NOT FXC_EXE)
    message(FATAL_ERROR "cg_compile_hlsl: fxc.exe not found")
  endif()

  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  set(_outputs)
  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_abs}" NAME_WE)

    _cg_guess_profile_from_name("${_abs}" _profile)
    set(_entry "main")
    set(_out   "${CG_OUTPUT_DIR}/${_base}.cso")

    # Config-aware flags that work in multi-config generators (VS)
    set(_fxc_flags
      /nologo /T ${_profile} /E ${_entry}
      /Fo "${_out}"
      "$<$<CONFIG:Debug>:/Od>" "$<$<CONFIG:Debug>:/Zi>"
      "$<$<NOT:$<CONFIG:Debug>>:/O3>"
    )

    foreach(_inc IN LISTS CG_INCLUDE_DIRS)
      list(APPEND _fxc_flags /I "${_inc}")
    endforeach()
    foreach(_def IN LISTS CG_DEFINES)
      list(APPEND _fxc_flags /D "${_def}")
    endforeach()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
      COMMAND "${FXC_EXE}" ${_fxc_flags} "${_abs}"
      MAIN_DEPENDENCY "${_abs}"
      COMMENT "FXC ${_profile}:${_entry} ${_base}.hlsl -> ${_base}.cso"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
endfunction()

# -----------------------------------------------------------------------------
# Public API: wire shader build target to the game target (+ copy/install)
# -----------------------------------------------------------------------------
function(cg_link_shaders_to_target SHADER_TARGET GAME_TARGET)
  if(TARGET ${SHADER_TARGET} AND TARGET ${GAME_TARGET})
    add_dependencies(${GAME_TARGET} ${SHADER_TARGET})

    # Copy compiled shaders next to the runtime on build
    add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders"
              "$<TARGET_FILE_DIR:${GAME_TARGET}>/shaders"
      COMMENT "Copying shaders to runtime directory"
      VERBATIM)

    # Install for packaging
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/"
            DESTINATION "bin/shaders")
  endif()
endfunction()
