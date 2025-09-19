# cmake/EnableWinQoL.cmake
# Minimal "Windows quality-of-life" utilities: resources, version info, asset staging, and a few
# MSVC options. All knobs are optional; the function is a no-op if inputs are missing.

if(NOT WIN32)
  return()
endif()

include(CMakeParseArguments)

function(_wq_normpath out path_in)
  if(NOT path_in)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  file(TO_CMAKE_PATH "${path_in}" _np)
  set(${out} "${_np}" PARENT_SCOPE)
endfunction()

function(enable_win_qol target product_name product_version)
  set(options SKIP_UNITY_FOR_BOOTSTRAP SECURITY_HARDENING FASTLINK_DEBUG WARNINGS_AS_ERRORS USE_STATIC_RUNTIME LTO ENABLE_ASAN)
  set(oneValueArgs COMPANY_NAME FILE_DESCRIPTION ICON WORKING_DIR MANIFEST)
  set(multiValueArgs ASSET_DIRS)
  cmake_parse_arguments(WQ "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT TARGET ${target})
    message(FATAL_ERROR "enable_win_qol: target '${target}' does not exist")
  endif()

  # ---------------- Version info resource ----------------
  # Parse product_version into 4 numbers
  set(_ver "${product_version}")
  string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.([0-9]+))?$" _m "${_ver}")
  if(NOT _m)
    # Fallback to 0.0.0.0 if malformed
    set(_v1 0) ; set(_v2 0) ; set(_v3 0) ; set(_v4 0)
  else()
    set(_v1 "${CMAKE_MATCH_1}")
    set(_v2 "${CMAKE_MATCH_2}")
    set(_v3 "${CMAKE_MATCH_3}")
    if(CMAKE_MATCH_5)
      set(_v4 "${CMAKE_MATCH_5}")
    else()
      set(_v4 0)
    endif()
  endif()

  if(NOT WQ_COMPANY_NAME)
    set(WQ_COMPANY_NAME "${product_name}")
  endif()
  if(NOT WQ_FILE_DESCRIPTION)
    set(WQ_FILE_DESCRIPTION "${product_name}")
  endif()

  set(_ver_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_versioninfo.rc")
  file(WRITE "${_ver_rc}" "
#include <winres.h>
VS_VERSION_INFO VERSIONINFO
 FILEVERSION ${_v1},${_v2},${_v3},${_v4}
 PRODUCTVERSION ${_v1},${_v2},${_v3},${_v4}
 FILEFLAGSMASK 0x3fL
#ifdef _DEBUG
 FILEFLAGS 0x1L
#else
 FILEFLAGS 0x0L
#endif
 FILEOS 0x4L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904b0\"
    BEGIN
      VALUE \"CompanyName\", \"${WQ_COMPANY_NAME}\"
      VALUE \"FileDescription\", \"${WQ_FILE_DESCRIPTION}\"
      VALUE \"FileVersion\", \"${_v1}.${_v2}.${_v3}.${_v4}\"
      VALUE \"InternalName\", \"${target}\"
      VALUE \"OriginalFilename\", \"${target}.exe\"
      VALUE \"ProductName\", \"${product_name}\"
      VALUE \"ProductVersion\", \"${_v1}.${_v2}.${_v3}.${_v4}\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x0409, 1200
  END
END
")
  target_sources(${target} PRIVATE "${_ver_rc}")
  source_group("Generated" FILES "${_ver_rc}")

  # ---------------- Icon resource (optional) ----------------
  if(WQ_ICON AND EXISTS "${WQ_ICON}")
    _wq_normpath(_icon "${WQ_ICON}")
    set(_ico_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc")
    file(WRITE "${_ico_rc}" "IDI_ICON1 ICON \"${_icon}\"\n")
    target_sources(${target} PRIVATE "${_ico_rc}")
    source_group("Generated" FILES "${_ico_rc}")
  endif()

  # ---------------- Asset mirroring next to the exe (optional) ----------------
  if(WQ_ASSET_DIRS)
    foreach(_dir IN LISTS WQ_ASSET_DIRS)
      if(EXISTS "${CMAKE_SOURCE_DIR}/${_dir}")
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/${_dir}"
          COMMAND "${CMAKE_COMMAND}" -E copy_directory
                  "${CMAKE_SOURCE_DIR}/${_dir}"
                  "$<TARGET_FILE_DIR:${target}>/${_dir}"
          COMMENT "Copying assets '${_dir}' next to $<TARGET_FILE_NAME:${target}>")
      endif()
    endforeach()
  endif()

  # ---------------- Debugger working dir (optional) ----------------
  if(MSVC AND WQ_WORKING_DIR)
    set_property(TARGET ${target} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${WQ_WORKING_DIR}")
  endif()

  # ---------------- Safety link/compile tweaks (optional) ----------------
  if(MSVC)
    if(WQ_SECURITY_HARDENING)
      target_link_options(${target} PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
    endif()
    if(WQ_FASTLINK_DEBUG)
      target_link_options(${target} PRIVATE "$<$<CONFIG:Debug>:/DEBUG:FASTLINK>")
    endif()
  endif()

  # Notes:
  # - We intentionally do NOT handle manifests here. The build uses a single explicit manifest via
  #   cg_embed_manifest() from cmake/win/EmbedManifest.cmake, as wired in CMakeLists.txt.
endfunction()
