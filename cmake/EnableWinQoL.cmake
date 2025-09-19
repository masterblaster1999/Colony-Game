# cmake/EnableWinQoL.cmake
#
# enable_win_qol(<target> <product_name> <product_version>
#                [COMPANY_NAME <name>] [FILE_DESCRIPTION <desc>]
#                [ICON <path.ico>] [WORKING_DIR <dir>]
#                [ASSET_DIRS <dir>...]
#                [SECURITY_HARDENING] [FASTLINK_DEBUG]
#                [WARNINGS_AS_ERRORS] [USE_STATIC_RUNTIME] [LTO] [ENABLE_ASAN]
#                [SKIP_UNITY_FOR_BOOTSTRAP])
#
include(CMakeParseArguments)

function(enable_win_qol tgt product_name product_version)
  set(options SECURITY_HARDENING FASTLINK_DEBUG WARNINGS_AS_ERRORS USE_STATIC_RUNTIME LTO ENABLE_ASAN SKIP_UNITY_FOR_BOOTSTRAP)
  set(oneValueArgs COMPANY_NAME FILE_DESCRIPTION ICON WORKING_DIR)
  set(multiValueArgs ASSET_DIRS)
  cmake_parse_arguments(WQ "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT TARGET ${tgt})
    message(FATAL_ERROR "enable_win_qol: target '${tgt}' not found")
  endif()

  # Nice-to-have warnings control
  if(MSVC AND WQ_WARNINGS_AS_ERRORS)
    target_compile_options(${tgt} PRIVATE /WX)
  endif()

  # Linker hardening
  if(MSVC AND WQ_SECURITY_HARDENING)
    target_link_options(${tgt} PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
  endif()

  # Fastlink for debug/development
  if(MSVC AND WQ_FASTLINK_DEBUG)
    target_link_options(${tgt} PRIVATE "$<$<CONFIG:Debug>:/DEBUG:FASTLINK>")
  endif()

  # Optional unity off for bootstrap (so small .cpps don't get batched oddly)
  if(WQ_SKIP_UNITY_FOR_BOOTSTRAP)
    if("${tgt}" MATCHES "WinLauncher")
      set_property(TARGET ${tgt} PROPERTY UNITY_BUILD OFF)
    endif()
  endif()

  # Embed icon + simple VERSIONINFO via RC (forward slash paths for rc.exe)
  if(WIN32)
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${tgt}_winqol.rc")
    set(_icon_section "")
    if(WQ_ICON AND EXISTS "${WQ_ICON}")
      file(TO_CMAKE_PATH "${WQ_ICON}" _icon_norm)
      set(_icon_section "IDI_APP_ICON ICON \"${_icon_norm}\"\n")
    endif()

    # Convert version "A.B.C.D" -> numbers; fallback to 0.1.0.0 form
    string(REGEX MATCH "^[0-9]+(\\.[0-9]+){0,3}" _ver_str "${product_version}")
    if(NOT _ver_str)
      set(_ver_str "0.1.0.0")
    endif()
    string(REPLACE "." "," _ver_commas "${_ver_str}")

    set(_company "${WQ_COMPANY_NAME}")
    if(NOT _company)
      set(_company "${product_name}")
    endif()
    set(_filedesc "${WQ_FILE_DESCRIPTION}")
    if(NOT _filedesc)
      set(_filedesc "${product_name}")
    endif()

    file(WRITE "${_rc}" "/* auto-generated */\n${_icon_section}
#include <winres.h>
VS_VERSION_INFO VERSIONINFO
 FILEVERSION ${_ver_commas}
 PRODUCTVERSION ${_ver_commas}
 FILEFLAGSMASK 0x3fL
 FILEOS 0x40004L
 FILETYPE 0x1L
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904b0\"
    BEGIN
      VALUE \"CompanyName\", \"${_company}\\0\"
      VALUE \"FileDescription\", \"${_filedesc}\\0\"
      VALUE \"FileVersion\", \"${_ver_str}\\0\"
      VALUE \"InternalName\", \"${tgt}\\0\"
      VALUE \"OriginalFilename\", \"${tgt}.exe\\0\"
      VALUE \"ProductName\", \"${product_name}\\0\"
      VALUE \"ProductVersion\", \"${_ver_str}\\0\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x0409, 1200
  END
END
")
    target_sources(${tgt} PRIVATE "${_rc}")
  endif()

  # Post-build: copy asset directories next to the exe
  foreach(_ad IN LISTS WQ_ASSET_DIRS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_ad}")
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/${_ad}" "$<TARGET_FILE_DIR:${tgt}>/${_ad}"
        VERBATIM)
    endif()
  endforeach()

  # Optional working dir (Visual Studio debugger)
  if(MSVC AND WQ_WORKING_DIR)
    set_property(TARGET ${tgt} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${WQ_WORKING_DIR}")
  endif()
endfunction()
