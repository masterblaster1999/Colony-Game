diff --git a/cmake/ColonyHLSL.cmake b/cmake/ColonyHLSL.cmake
new file mode 100644
index 0000000..b1c0c9a
--- /dev/null
+++ b/cmake/ColonyHLSL.cmake
@@ -0,0 +1,332 @@
+# Colony-Game — HLSL (DXC) build helpers for Windows
+# Standardizes Shader Model 6.x compilation via dxc.exe
+# Outputs: .dxil (primary), .cso (compat alias), and a C header with embedded bytes.
+#
+# Requirements:
+#   * Windows toolchain (MSVC/VS or Ninja + MSVC)
+#   * DXC available either via:
+#       - vcpkg 'directx-dxc' port (preferred): provides DIRECTX_DXC_TOOL CMake var
+#       - PATH / common SDK locations
+#
+# References:
+#   - DXC (DirectXShaderCompiler) project/wiki (modern HLSL/SM6 compiler). 
+#   - dxc.exe options: -Fh (emit header), -Vn (header variable), -Zi/-Fd (debug),
+#                      -O0..-O3 (opt), -Qembed_debug, -Qstrip_debug, -Qstrip_reflect, etc.
+#   - Enable -Zi in Debug for GPU tooling (Nsight/PIX); strip in Release to trim blobs.
+#
+# Docs / sources:
+#   DXC project/wiki: https://github.com/microsoft/DirectXShaderCompiler
+#   dxc.exe options incl. -Fh / -Vn: https://strontic.github.io/xcyclopedia/library/dxc.exe-0C1709D4E1787E3EB3E6A35C85714824.html
+#   Nsight on needing -Zi: https://developer.nvidia.com/blog/harness-powerful-shader-insights-using-shader-debug-info-with-nvidia-nsight-graphics/
+#   vcpkg 'directx-dxc' port (DIRECTX_DXC_TOOL): https://vcpkg.link/ports/directx-dxc
+#
+# Public API:
+#   colony_add_hlsl_shaders(
+#       TARGET <target>
+#       SOURCES <file1.hlsl> [file2.hlsl ...]
+#       [ROOT <path>]                       # base for SOURCE_GROUP and relative paths
+#       [INCLUDE_DIRS <dir> ...]            # -I include dirs
+#       [DEFINES     <DEF> ...]             # -D macro=val
+#       [SHADER_MODEL 6_6]                  # default 6_8 if not set
+#       [HEADER_NAMESPACE myns]             # optional namespace for generated header arrays
+#   )
+#
+# Conventions:
+#   Stage inferred from filename suffix:
+#     *.vs.hlsl -> vs, *.ps.hlsl -> ps, *.cs.hlsl -> cs, *.gs.hlsl -> gs,
+#     *.ds.hlsl -> ds, *.hs.hlsl -> hs, *.ms.hlsl -> ms, *.as.hlsl -> as
+#   Entry point: default "main", or set per-file:
+#     set_source_files_properties(shaders/foo.ps.hlsl PROPERTIES HLSL_ENTRY myps)
+#   Per-file defines (optional):
+#     set_source_files_properties(shaders/foo.ps.hlsl PROPERTIES HLSL_DEFINES "FOO=1;BAR=2")
+#
+
+include_guard(GLOBAL)
+
+function(_colony_detect_dxc out_var)
+    # Try vcpkg's 'directx-dxc' port first (exposes DIRECTX_DXC_TOOL)
+    set(_dxc "")
+    find_package(directx-dxc CONFIG QUIET)
+    if(DEFINED DIRECTX_DXC_TOOL)
+        set(_dxc "${DIRECTX_DXC_TOOL}")
+    endif()
+    if(NOT _dxc)
+        # Fallback: typical tool names/locations
+        # Note: VCPKG_INSTALLED_DIR/VCPKG_TARGET_TRIPLET are defined when using vcpkg toolchain.
+        find_program(_dxc NAMES dxc dxc.exe
+            HINTS
+                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
+                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directxshadercompiler"
+                "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
+                "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directxshadercompiler"
+                "$ENV{ProgramFiles}/Microsoft DirectX Shader Compiler"
+                "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/x64"
+        )
+    endif()
+    if(NOT _dxc)
+        message(FATAL_ERROR
+            "dxc.exe not found. Install vcpkg port 'directx-dxc' (preferred) or add DXC to PATH.")
+    endif()
+    set(${out_var} "${_dxc}" PARENT_SCOPE)
+endfunction()
+
+function(_colony_stage_from_filename out_stage out_profile filename shader_model)
+    string(TOLOWER "${filename}" _fname)
+    # Match suffix .<stage>.hlsl
+    string(REGEX MATCH "\\.([avdghmp]s)\\.hlsl$" _m "${_fname}")
+    if(NOT _m)
+        message(FATAL_ERROR
+            "Cannot infer shader stage from '${filename}'. "
+            "Use one of: *.vs.hlsl, *.ps.hlsl, *.cs.hlsl, *.gs.hlsl, *.ds.hlsl, *.hs.hlsl, *.ms.hlsl, *.as.hlsl")
+    endif()
+    string(REGEX REPLACE ".*\\.([avdghmp]s)\\.hlsl$" "\\1" _stage "${_fname}")
+    set(${out_stage}   "${_stage}"               PARENT_SCOPE)
+    set(${out_profile} "${_stage}_${shader_model}" PARENT_SCOPE)
+endfunction()
+
+function(_colony_sanitize_varname out_var raw)
+    # Make a valid C identifier for header variable name
+    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _var "${raw}")
+    # Cannot start with digit
+    string(REGEX MATCH "^[0-9]" _starts_digit "${_var}")
+    if(_starts_digit)
+        set(_var "g_${_var}")
+    endif()
+    set(${out_var} "${_var}" PARENT_SCOPE)
+endfunction()
+
+function(colony_add_hlsl_shaders)
+    set(_opts     )
+    set(_oneval   TARGET ROOT SHADER_MODEL DXC_EXE HEADER_NAMESPACE)
+    set(_multival SOURCES INCLUDE_DIRS DEFINES)
+    cmake_parse_arguments(COLONY "${_opts}" "${_oneval}" "${_multival}" ${ARGN})
+
+    if(NOT COLONY_TARGET)
+        message(FATAL_ERROR "colony_add_hlsl_shaders: TARGET is required")
+    endif()
+    if(NOT COLONY_SOURCES)
+        message(FATAL_ERROR "colony_add_hlsl_shaders: SOURCES list is empty")
+    endif()
+
+    # DXC discovery
+    if(COLONY_DXC_EXE)
+        set(_DXC "${COLONY_DXC_EXE}")
+    else()
+        _colony_detect_dxc(_DXC)
+    endif()
+
+    # Shader model (default 6_8)
+    if(COLONY_SHADER_MODEL)
+        set(_SM "${COLONY_SHADER_MODEL}")
+    else()
+        set(_SM "6_8")
+    endif()
+
+    # Include dirs / defines
+    set(_inc_args "")
+    foreach(_dir IN LISTS COLONY_INCLUDE_DIRS)
+        list(APPEND _inc_args "-I" "$<SHELL_PATH:${_dir}>")
+    endforeach()
+
+    set(_def_args "")
+    foreach(_d IN LISTS COLONY_DEFINES)
+        list(APPEND _def_args "-D" "${_d}")
+    endforeach()
+
+    # Output dir in binary tree
+    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
+    file(MAKE_DIRECTORY "${_out_dir}")
+
+    # Generator expressions for config‑specific flags
+    set(_dbg_flags "$<$<CONFIG:Debug>:-Zi;-Qembed_debug;-Od>")
+    set(_rel_flags "$<$<CONFIG:Release>:-O3;-Qstrip_debug;-Qstrip_reflect>")
+    # PDB path only in Debug
+    set(_pdb_flag  "$<$<CONFIG:Debug>:-Fd$<SHELL_PATH:${_out_dir}/>>")
+
+    # Optional C++ namespace for the generated header arrays
+    set(_ns_open "")
+    set(_ns_close "")
+    if(COLONY_HEADER_NAMESPACE)
+        set(_ns_open  "// generated by CMake\nnamespace ${COLONY_HEADER_NAMESPACE} {\n")
+        set(_ns_close "\n} // namespace ${COLONY_HEADER_NAMESPACE}\n")
+    endif()
+
+    # Track outputs for a phony ALL target
+    set(_all_outputs "")
+    set(_all_headers "")
+
+    foreach(_src IN LISTS COLONY_SOURCES)
+        # Resolve absolute path relative to optional ROOT
+        if(COLONY_ROOT)
+            get_filename_component(_src_abs "${_src}" ABSOLUTE BASE_DIR "${COLONY_ROOT}")
+        else()
+            get_filename_component(_src_abs "${_src}" ABSOLUTE)
+        endif()
+        if(NOT EXISTS "${_src_abs}")
+            message(FATAL_ERROR "HLSL source not found: '${_src}' (resolved to '${_src_abs}')")
+        endif()
+
+        get_filename_component(_fname "${_src_abs}" NAME)
+        get_filename_component(_stem  "${_src_abs}" NAME_WE) # still includes ".vs" in NAME_WE, which is fine
+
+        # Per‑file overrides
+        get_source_file_property(_entry "${_src_abs}" HLSL_ENTRY)
+        if(NOT _entry OR _entry STREQUAL "NOTFOUND")
+            set(_entry "main")
+        endif()
+        get_source_file_property(_file_defs_raw "${_src_abs}" HLSL_DEFINES)
+        set(_file_def_args "")
+        if(_file_defs_raw AND NOT _file_defs_raw STREQUAL "NOTFOUND")
+            # Split on ';'
+            string(REPLACE ";" ";" _file_defs_list "${_file_defs_raw}")
+            foreach(_fd IN LISTS _file_defs_list)
+                list(APPEND _file_def_args "-D" "${_fd}")
+            endforeach()
+        endif()
+
+        # Determine stage/profile from filename suffix
+        _colony_stage_from_filename(_stage _profile "${_fname}" "${_SM}")
+
+        # Outputs
+        set(_dxil   "${_out_dir}/${_fname}.dxil")
+        set(_cso    "${_out_dir}/${_fname}.cso")  # compat alias (copy of dxil)
+        set(_header "${_out_dir}/${_fname}.h")
+        set(_pdb    "${_out_dir}/${_fname}.pdb")
+
+        # Header var name
+        _colony_sanitize_varname(_var "g_${_fname}_${_profile}")
+
+        # Custom command: compile with DXC
+        add_custom_command(
+            OUTPUT "${_dxil}" "${_header}" "${_cso}"
+            COMMAND "${_DXC}"
+                -E "${_entry}"
+                -T "${_profile}"
+                ${_inc_args}
+                ${_def_args} ${_file_def_args}
+                ${_dbg_flags} ${_rel_flags}
+                -Fo "$<SHELL_PATH:${_dxil}>"
+                -Fh "$<SHELL_PATH:${_header}>"
+                -Vn "${_var}"
+                ${_pdb_flag}
+                "$<SHELL_PATH:${_src_abs}>"
+            # Mirror dxil → cso for legacy loader paths expecting *.cso
+            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<SHELL_PATH:${_dxil}>" "$<SHELL_PATH:${_cso}>"
+            DEPENDS "${_src_abs}"
+            BYPRODUCTS "${_pdb}"
+            COMMENT "DXC ${_fname}  →  ${_profile}"
+            VERBATIM
+        )
+
+        # Prepend/append namespace boilerplate to the header (once)
+        # dxc's -Fh emits just the array; we wrap it if a namespace was requested.
+        if(COLONY_HEADER_NAMESPACE)
+            add_custom_command(
+                TARGET ${COLONY_TARGET} PRE_BUILD
+                COMMAND "${CMAKE_COMMAND}" -DHEADER="$<SHELL_PATH:${_header}>"
+                                          -DNS_OPEN="${_ns_open}"
+                                          -DNS_CLOSE="${_ns_close}"
+                                          -P "${CMAKE_CURRENT_LIST_DIR}/WrapHeaderNamespace.cmake"
+                BYPRODUCTS "${_header}"
+                COMMENT "Wrapping header namespace for ${_fname}"
+                VERBATIM
+            )
+        endif()
+
+        list(APPEND _all_outputs "${_dxil}" "${_cso}")
+        list(APPEND _all_headers "${_header}")
+    endforeach()
+
+    # Group in IDE, include binary shaders/headers for compilation units
+    if(COLONY_ROOT)
+        source_group(TREE "${COLONY_ROOT}" FILES ${COLONY_SOURCES})
+    endif()
+    # Make headers visible to the target (include directory is the binary shaders dir)
+    target_include_directories(${COLONY_TARGET} PRIVATE "${_out_dir}")
+
+    # Aggregate target to ensure shaders build before the main target
+    add_custom_target(${COLONY_TARGET}_shaders ALL DEPENDS ${_all_outputs} ${_all_headers})
+    add_dependencies(${COLONY_TARGET} ${COLONY_TARGET}_shaders)
+endfunction()
+
+# Helper script to wrap generated headers with an optional namespace.
+# Creates a temp file, writes ns_open + file + ns_close, then replaces.
+#
+# Usage:
+#   cmake -DHEADER=path -DNS_OPEN="text" -DNS_CLOSE="text" -P WrapHeaderNamespace.cmake
+#
+if(CMAKE_SCRIPT_MODE_FILE)
+    if(DEFINED HEADER AND DEFINED NS_OPEN AND DEFINED NS_CLOSE)
+        file(READ  "${HEADER}" _body)
+        file(WRITE "${HEADER}.tmp" "${NS_OPEN}${_body}${NS_CLOSE}")
+        file(RENAME "${HEADER}.tmp" "${HEADER}")
+    endif()
+endif()
