# cmake/EnableWinQoL.cmake
#
# Windows/MSVC-only “Quality of Life” setup for game executables.
# Back-compat call style:
#   enable_win_qol(<target> "<AppName>" "<AppVersion>" [OPTIONS...])
#
# New options (all optional):
#   COMPANY_NAME        "Your Studio, Inc."
#   FILE_DESCRIPTION    "Colony-Game"
#   ICON                path/to/app.ico
#   MANIFEST            path/to/app.manifest  (if omitted, an advanced default is generated)
#   PCH                 path/to/pch.h         (requires CMake >= 3.16)
#   WORKING_DIR         VS debugger working dir; default: $<TARGET_FILE_DIR:target>
#   ASSET_DIRS          list of asset directories to copy next to the exe after build
#                       NOTE: The top-level 'res' directory is copied ONCE per output base
#                             directory in a race-free manner; if 'res' appears in ASSET_DIRS
#                             it will be filtered out to avoid duplicate copies.
#   LINK_LIBS           extra Windows libs to link (Dbghelp & Shell32 are added automatically)
#   EXTRA_DEFINES       extra compile definitions
#
#   Flags (ON if specified):
#   USE_STATIC_RUNTIME  Use /MT(d) via MSVC_RUNTIME_LIBRARY (needs CMake >= 3.15)
#   LTO                 Enable interprocedural optimization for Release/RelWithDebInfo
#   FASTLINK_DEBUG      Use /DEBUG:FASTLINK in Debug/RelWithDebInfo (else /DEBUG:FULL)
#   WARNINGS_AS_ERRORS  Treat warnings as errors (/WX)
#   SPECTRE             Enable /Qspectre
#   ENABLE_ASAN         Enable AddressSanitizer for Debug x64 (/fsanitize=address)
#   CONSOLE_IN_DEBUG    Force /SUBSYSTEM:CONSOLE in Debug (keep windows subsystem in others)
#   SKIP_UNITY_FOR_BOOTSTRAP  Mark WinBootstrap.cpp as SKIP_UNITY_BUILD_INCLUSION
#   ENABLE_SDL          Enable /sdl (extra security checks); included by SECURITY_HARDENING
#   SECURITY_HARDENING  Bundle recommended compile/link security flags (/sdl, /guard:cf, etc)
#
# Requires: MSVC on Windows.
# Safe on older CMake versions (guards included).
#
# NOTE: For .rc content we intentionally emit **forward slashes** by using
#       `file(TO_CMAKE_PATH ...)` to avoid backslash escape sequences such as
#       \a, \b, \t being interpreted by rc.exe in string literals.
#
# ------------------------------------------------------------------------------

include_guard(GLOBAL)

# Small helper: write a file only if content changed (prevents rebuild churn)
function(_winqol_write_if_different out_path content)
  if (EXISTS "${out_path}")
    file(READ "${out_path}" _old)
  else()
    set(_old "")
  endif()
  if (NOT _old STREQUAL "${content}")
    file(WRITE "${out_path}" "${content}")
  endif()
endfunction()

# Generate a good default app.manifest if the project doesn't provide one
function(_winqol_generate_manifest out_manifest app_name file_description)
  set(_manifest "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<assembly manifestVersion=\"1.0\" xmlns=\"urn:schemas-microsoft-com:asm.v1\" xmlns:asmv3=\"urn:schemas-microsoft-com:asm.v3\">
  <assemblyIdentity version=\"1.0.0.0\" processorArchitecture=\"*\" name=\"${app_name}\" type=\"win32\"/>
  <description>${file_description}</description>
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <asmv3:application>
    <asmv3:windowsSettings>
      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">PerMonitorV2</dpiAware>
      <longPathAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware>
      <gdiScaling xmlns=\"http://schemas.microsoft.com/SMI/2017/WindowsSettings\">true</gdiScaling>
      <heapType xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">SegmentHeap</heapType>
      <highResolutionScrollingAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</highResolutionScrollingAware>
    </asmv3:windowsSettings>
  </asmv3:application>
</assembly>
")
  _winqol_write_if_different("${out_manifest}" "${_manifest}")
endfunction()

# Generate a version.rc (with optional icon + manifest embedding)
function(_winqol_generate_version_rc out_rc app_name app_version exe_name icon_path manifest_path company filedesc)
  string(REPLACE "." "," _v_commas "${app_version}")
  if (icon_path)
    # Use forward slashes in .rc string literals to avoid escape sequences.
    file(TO_CMAKE_PATH "${icon_path}" _icon_win)
    set(_icon_line "IDI_APPICON ICON \"${_icon_win}\"\n")
  else()
    set(_icon_line "")
  endif()

  if (manifest_path)
    # Use forward slashes in .rc string literals to avoid escape sequences.
    file(TO_CMAKE_PATH "${manifest_path}" _man_win)
    # 1 = CREATEPROCESS_MANIFEST_RESOURCE_ID; RT_MANIFEST ensures correct embedding
    set(_manifest_line "CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST \"${_man_win}\"\n")
  else()
    set(_manifest_line "")
  endif()

  set(_rc "///////////////////////////////////////////////////////////////////////////////
// Auto-generated by EnableWinQoL.cmake

#include <winres.h>

${_icon_line}${_manifest_line}
VS_VERSION_INFO VERSIONINFO
 FILEVERSION ${_v_commas}
 PRODUCTVERSION ${_v_commas}
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
      VALUE \"CompanyName\",      \"${company}\"
      VALUE \"FileDescription\",   \"${filedesc}\"
      VALUE \"FileVersion\",       \"${app_version}\"
      VALUE \"InternalName\",      \"${app_name}\"
      VALUE \"OriginalFilename\",  \"${exe_name}\"
      VALUE \"ProductName\",       \"${app_name}\"
      VALUE \"ProductVersion\",    \"${app_version}\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x0409, 1200
  END
END
")
  _winqol_write_if_different("${out_rc}" "${_rc}")
endfunction()

# ------------------------------------------------------------------------------
# NEW: Copy the top-level 'res' directory ONCE per runtime output base dir.
# All Windows EXECUTABLE targets can depend on this; the copy won't run concurrently.
# Uses RUNTIME_OUTPUT_DIRECTORY as the base when set; otherwise falls back to
# <build>/bin. Copies into "<base>/$<CONFIG>/res" for multi-config generators.
# ------------------------------------------------------------------------------
function(_winqol_setup_res_copy_once target)
  if (NOT WIN32)
    return()
  endif()

  # Limit to executables to avoid surprising DLL-only projects.
  get_target_property(_tgt_type "${target}" TYPE)
  if (NOT _tgt_type STREQUAL "EXECUTABLE")
    return()
  endif()

  # If the project doesn't have a 'res' dir, do nothing.
  if (NOT EXISTS "${CMAKE_SOURCE_DIR}/res")
    return()
  endif()

  # Determine the base runtime output directory (e.g. <build>/bin)
  get_target_property(_out_base "${target}" RUNTIME_OUTPUT_DIRECTORY)  # may be unset or a genex
  if (NOT _out_base)
    set(_out_base "${CMAKE_BINARY_DIR}/bin")
  endif()

  # Create a unique custom target per output base dir.
  # We still copy to "<base>/$<CONFIG>/res" so multi-config works.
  string(MD5 _out_hash "${_out_base}")
  set(_copy_tgt "winqol_copy_res_${_out_hash}")

  if (NOT TARGET "${_copy_tgt}")
    add_custom_target("${_copy_tgt}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_base}/$<CONFIG>/res"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${CMAKE_SOURCE_DIR}/res" "${_out_base}/$<CONFIG>/res"
      COMMENT "[EnableWinQoL] Copying assets 'res' to ${_out_base}/$<CONFIG>/res"
      VERBATIM
    )
  endif()

  # Ensure this target builds only after the 'res' assets are staged.
  add_dependencies("${target}" "${_copy_tgt}")
endfunction()

# Copy a directory to the target output folder (post-build) — generic helper
function(_winqol_copy_dir_post_build target src_dir)
  if (NOT IS_DIRECTORY "${src_dir}")
    message(WARNING "[EnableWinQoL] ASSET_DIR '${src_dir}' does not exist; skipping.")
    return()
  endif()
  get_filename_component(_base "${src_dir}" NAME)
  add_custom_command(TARGET "${target}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/${_base}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${src_dir}" "$<TARGET_FILE_DIR:${target}>/${_base}"
    COMMENT "[EnableWinQoL] Copying assets '${_base}' to output directory"
    VERBATIM)
endfunction()

# Main API
function(enable_win_qol target app_name app_version)
  if (NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "enable_win_qol is Windows/MSVC-only.")
  endif()

  # ---------------- Parse optional arguments ----------------
  set(_opts
    USE_STATIC_RUNTIME LTO FASTLINK_DEBUG WARNINGS_AS_ERRORS SPECTRE ENABLE_ASAN
    CONSOLE_IN_DEBUG SKIP_UNITY_FOR_BOOTSTRAP ENABLE_SDL SECURITY_HARDENING
  )
  set(_one
    COMPANY_NAME FILE_DESCRIPTION ICON MANIFEST PCH WORKING_DIR
  )
  set(_multi
    ASSET_DIRS LINK_LIBS EXTRA_DEFINES
  )
  cmake_parse_arguments(WQ "${_opts}" "${_one}" "${_multi}" ${ARGN})

  if (NOT TARGET "${target}")
    message(FATAL_ERROR "[EnableWinQoL] Target '${target}' not found.")
  endif()

  # ---------------- Defaults ----------------
  if (NOT WQ_COMPANY_NAME)
    set(WQ_COMPANY_NAME "Unknown Studio")
  endif()
  if (NOT WQ_FILE_DESCRIPTION)
    set(WQ_FILE_DESCRIPTION "${app_name}")
  endif()
  if (NOT WQ_WORKING_DIR)
    set(WQ_WORKING_DIR "$<TARGET_FILE_DIR:${target}>")
  endif()
  if (WQ_SECURITY_HARDENING)
    set(WQ_ENABLE_SDL ON)  # implicit
  endif()

  # ---------------- Common Windows headers & defines ----------------
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    APP_NAME_W=L"${app_name}"
    APP_VERSION_W=L"${app_version}"
  )
  if (WQ_EXTRA_DEFINES)
    target_compile_definitions(${target} PRIVATE ${WQ_EXTRA_DEFINES})
  endif()

  # Warn/UTF-8/Conformance/Exceptions
  target_compile_options(${target} PRIVATE
    /W4 /permissive- /EHsc /utf-8
    /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:throwingNew /volatile:iso
  )
  if (WQ_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE /WX)
  endif()
  if (WQ_ENABLE_SDL)
    target_compile_options(${target} PRIVATE /sdl)
  endif()
  if (WQ_SPECTRE)
    target_compile_options(${target} PRIVATE /Qspectre)
  endif()

  # ---------------- Linker hardening & debug settings ----------------
  # Security hardening
  target_link_options(${target} PRIVATE
    /guard:cf           # Control Flow Guard
    /dynamicbase        # ASLR
    /nxcompat           # DEP
    /highentropyva      # 64-bit ASLR improvements
  )

  # Debug info format
  if (WQ_FASTLINK_DEBUG)
    target_link_options(${target} PRIVATE
      "$<$<CONFIG:Debug,RelWithDebInfo>:/DEBUG:FASTLINK>"
    )
  else()
    target_link_options(${target} PRIVATE
      "$<$<CONFIG:Debug,RelWithDebInfo>:/DEBUG:FULL>"
    )
  endif()

  # Console window for Debug builds (handy for logs/printf)
  if (WQ_CONSOLE_IN_DEBUG)
    target_link_options(${target} PRIVATE
      "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
    )
  endif()

  # ---------------- MSVC runtime selection ----------------
  if (WQ_USE_STATIC_RUNTIME)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
      # /MT[d]
      set_property(TARGET ${target} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
      message(WARNING "[EnableWinQoL] Static runtime requested but CMake < 3.15; "
                      "consider upgrading (falling back to toolchain defaults).")
    endif()
  endif()

  # ---------------- LTO / IPO ----------------
  if (WQ_LTO)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.9")
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      if (CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
      endif()
    else()
      message(WARNING "[EnableWinQoL] LTO requested but CMake < 3.9; skipping.")
    endif()
  endif()

  # ---------------- AddressSanitizer (x64 Debug only) ----------------
  if (WQ_ENABLE_ASAN)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
      target_link_options(${target}    PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
    else()
      message(WARNING "[EnableWinQoL] ASAN is only supported on x64 MSVC; skipping.")
    endif()
  endif()

  # ---------------- Bootstrap wiring (if your repo has it) ----------------
  set(_proj_root "${CMAKE_SOURCE_DIR}")
  set(_bootstrap_cpp "${_proj_root}/platform/win/WinBootstrap.cpp")
  set(_bootstrap_dir "${_proj_root}/platform/win")

  if (EXISTS "${_bootstrap_cpp}")
    target_sources(${target} PRIVATE "${_bootstrap_cpp}")
    if (EXISTS "${_bootstrap_dir}")
      target_include_directories(${target} PRIVATE "${_bootstrap_dir}")
    endif()
    if (WQ_SKIP_UNITY_FOR_BOOTSTRAP)
      set_source_files_properties("${_bootstrap_cpp}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
  else()
    message(STATUS "[EnableWinQoL] platform/win/WinBootstrap.cpp not found; skipping bootstrap wiring.")
  endif()

  # (Optional) If you expose CrashHandler headers in src/os/win, include that folder
  if (EXISTS "${_proj_root}/src/os/win")
    target_include_directories(${target} PRIVATE "${_proj_root}/src/os/win")
  endif()
  # (Optional) If you keep tiny launcher helpers in src/launcher
  if (EXISTS "${_proj_root}/src/launcher")
    target_include_directories(${target} PRIVATE "${_proj_root}/src/launcher")
  endif()

  # Always link Windows libs commonly used by bootstrap & path helpers
  set(_win_libs Dbghelp Shell32)
  if (WQ_LINK_LIBS)
    list(APPEND _win_libs ${WQ_LINK_LIBS})
  endif()
  target_link_libraries(${target} PRIVATE ${_win_libs})

  # ---------------- Manifest / Version / Icon handling ----------------
  # Prefer project-provided resources if present; otherwise generate high-quality defaults.
  set(_res_dir "${_proj_root}/res")
  set(_have_project_manifest FALSE)
  set(_have_project_version FALSE)

  if (WQ_MANIFEST)
    set(_manifest_in "${WQ_MANIFEST}")
  elseif (EXISTS "${_res_dir}/app.manifest")
    set(_manifest_in "${_res_dir}/app.manifest")
    set(_have_project_manifest TRUE)
  else()
    set(_manifest_in "")
  endif()

  if (EXISTS "${_res_dir}/version.rc")
    set(_version_rc_in "${_res_dir}/version.rc")
    set(_have_project_version TRUE)
  else()
    set(_version_rc_in "")
  endif()

  # Where we place generated artifacts
  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/winqol/${target}")
  file(MAKE_DIRECTORY "${_gen_dir}")

  # Generate manifest if needed
  if (NOT _manifest_in)
    set(_manifest_in "${_gen_dir}/app.generated.manifest")
    _winqol_generate_manifest("${_manifest_in}" "${app_name}" "${WQ_FILE_DESCRIPTION}")
  endif()

  # If project already has a complete version.rc, just use it.
  # Otherwise, generate a compact, correct one that also embeds the manifest and icon.
  if (_have_project_version)
    target_sources(${target} PRIVATE "${_version_rc_in}")
    # Also ensure the manifest is compiled (in case their version.rc doesn't include it)
    # We generate a tiny RC that only embeds the manifest, to avoid double VS_VERSION_INFO.
    set(_man_embed_rc "${_gen_dir}/embed_manifest_only.rc")
    # Use forward slashes in .rc string literals to avoid escape sequences.
    file(TO_CMAKE_PATH "${_manifest_in}" _man_native)
    set(_rc_small "CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST \"${_man_native}\"\n")
    _winqol_write_if_different("${_man_embed_rc}" "${_rc_small}")
    target_sources(${target} PRIVATE "${_man_embed_rc}")
    if (WQ_ICON AND EXISTS "${WQ_ICON}")
      # Let the main version.rc own the icon if it does; otherwise add a tiny icon rc.
      set(_icon_rc "${_gen_dir}/embed_icon_only.rc")
      # Use forward slashes in .rc string literals to avoid escape sequences.
      file(TO_CMAKE_PATH "${WQ_ICON}" _icon_native)
      set(_rc_icon "IDI_APPICON ICON \"${_icon_native}\"\n")
      _winqol_write_if_different("${_icon_rc}" "${_rc_icon}")
      target_sources(${target} PRIVATE "${_icon_rc}")
    endif()
  else()
    # Generate a full version.rc (with icon + manifest)
    if (WQ_ICON AND EXISTS "${WQ_ICON}")
      set(_icon_for_gen "${WQ_ICON}")
    else()
      set(_icon_for_gen "")
    endif()
    # exe filename for metadata
    get_target_property(_target_type ${target} TYPE)
    if (_target_type STREQUAL "EXECUTABLE")
      set(_exe_name "${app_name}.exe")
    else()
      set(_exe_name "${app_name}.dll")
    endif()

    set(_gen_rc "${_gen_dir}/winqol_version.rc")
    _winqol_generate_version_rc("${_gen_rc}" "${app_name}" "${app_version}" "${_exe_name}"
                                "${_icon_for_gen}" "${_manifest_in}"
                                "${WQ_COMPANY_NAME}" "${WQ_FILE_DESCRIPTION}")
    target_sources(${target} PRIVATE "${_gen_rc}")
  endif()

  # ---------------- Visual Studio QoL ----------------
  set_property(TARGET ${target} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${WQ_WORKING_DIR}")

  # Optional PCH
  if (WQ_PCH)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
      target_precompile_headers(${target} PRIVATE "${WQ_PCH}")
    else()
      message(WARNING "[EnableWinQoL] PCH requires CMake >= 3.16; skipping.")
    endif()
  endif()

  # ---------------- Asset staging ----------------
  # First, set up the one-time 'res' copy for executables (if res/ exists).
  _winqol_setup_res_copy_once(${target})

  # Then, copy any additional asset directories per-target, but filter out 'res'
  # to avoid duplicating the one-time copy above.
  if (WQ_ASSET_DIRS)
    # Compute absolute path to the project's top-level res.
    get_filename_component(_res_abs "${CMAKE_SOURCE_DIR}/res" ABSOLUTE)
    string(TOLOWER "${_res_abs}" _res_abs_l)

    set(_asset_dirs_filtered "")
    foreach(_ad IN LISTS WQ_ASSET_DIRS)
      # Normalize to absolute for comparison; relative paths are resolved from source dir.
      get_filename_component(_ad_abs "${_ad}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
      string(TOLOWER "${_ad_abs}" _ad_abs_l)

      # Skip duplicates of 'res' (relative 'res', './res', or absolute match).
      if (_ad STREQUAL "res" OR _ad STREQUAL "./res" OR _ad_abs_l STREQUAL _res_abs_l)
        # filtered out
      else()
        list(APPEND _asset_dirs_filtered "${_ad}")
      endif()
    endforeach()

    foreach(_ad IN LISTS _asset_dirs_filtered)
      _winqol_copy_dir_post_build(${target} "${_ad}")
    endforeach()
  endif()

  # ---------------- Install helpers (optional) ----------------
  # If you use `cmake --install`, these will place exe, dependent DLLs, and PDBs in bin/
  include(GNUInstallDirs OPTIONAL RESULT_VARIABLE _gnuinc)
  if (_gnuinc)
    install(TARGETS ${target}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      BUNDLE  DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Copy runtime dependencies (modern CMake will resolve msvc/redist + third-party DLLs)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
      install(TARGETS ${target} RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*system32/.*"
        DIRECTORIES "${CMAKE_SOURCE_DIR}"
      )
    endif()

    # Install PDBs when available (Debug/RelWithDebInfo)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
      install(FILES $<TARGET_PDB_FILE:${target}> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
    endif()
  endif()

  message(STATUS "[EnableWinQoL] Applied to '${target}' (App='${app_name}', Version='${app_version}').")
endfunction()
