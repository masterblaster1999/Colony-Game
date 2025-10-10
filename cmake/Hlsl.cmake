# cmake/Hlsl.cmake  (Improved & future‑proof)
cmake_minimum_required(VERSION 3.19)

# Global defaults
option(USE_DXC "Global default: prefer DXC when found" ON)
set(DXC_SHADER_MODEL "6_0" CACHE STRING "Default SM for DXC (e.g., 6_0, 6_6)")
set(FXC_SHADER_MODEL "5_0" CACHE STRING "Default SM for FXC (e.g., 5_0, 5_1)")

# ---------- JSON helpers ----------
function(_json_try_type out json)
  string(JSON _t ERROR_VARIABLE _err TYPE "${json}" ${ARGN})
  if(_err)  set(${out} "" PARENT_SCOPE) else() set(${out} "${_t}" PARENT_SCOPE) endif()
endfunction()
function(_json_try_get out json)
  string(JSON _v ERROR_VARIABLE _err GET "${json}" ${ARGN})
  if(_err)  set(${out} "" PARENT_SCOPE) else() set(${out} "${_v}" PARENT_SCOPE) endif()
endfunction()
function(_json_array_length out json)
  string(JSON _len ERROR_VARIABLE _err LENGTH "${json}" ${ARGN})
  if(_err) set(${out} 0 PARENT_SCOPE) else() set(${out} "${_len}" PARENT_SCOPE) endif()
endfunction()

# ---------- Stage suffix / normalization ----------
function(_norm_stage in out)
  string(TOLOWER "${in}" _s)
  if(_s MATCHES "^(v|vs|vertex)$")      set(${out} "vs" PARENT_SCOPE)
  elseif(_s MATCHES "^(p|ps|pixel)$")   set(${out} "ps" PARENT_SCOPE)
  elseif(_s MATCHES "^(c|cs|compute)$") set(${out} "cs" PARENT_SCOPE)
  elseif(_s MATCHES "^(d|ds|domain)$")  set(${out} "ds" PARENT_SCOPE)
  elseif(_s MATCHES "^(h|hs|hull)$")    set(${out} "hs" PARENT_SCOPE)
  elseif(_s MATCHES "^(g|gs|geometry)$")set(${out} "gs" PARENT_SCOPE)
  else()                                set(${out} ""  PARENT_SCOPE)
  endif()
endfunction()

function(_stage_suffix out stage)
  string(TOLOWER "${stage}" _s)
  if(_s STREQUAL "vs") set(${out} "VS" PARENT_SCOPE)
  elseif(_s STREQUAL "ps") set(${out} "PS" PARENT_SCOPE)
  elseif(_s STREQUAL "cs") set(${out} "CS" PARENT_SCOPE)
  elseif(_s STREQUAL "ds") set(${out} "DS" PARENT_SCOPE)
  elseif(_s STREQUAL "hs") set(${out} "HS" PARENT_SCOPE)
  elseif(_s STREQUAL "gs") set(${out} "GS" PARENT_SCOPE)
  else() set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# ---------- Inference from filename ----------
function(_infer_stage_and_entry rel out_stage out_entry)
  string(TOLOWER "${rel}" _rel)
  set(_stage "") ; set(_entry "")
  if(_rel MATCHES "(^|/)(cs[^/]*|[^/]*_cs)\\.hlsl$")              set(_stage "cs") set(_entry "main")
  elseif(_rel MATCHES "(^|/)(vs[^/]*|[^/]*_vs)\\.hlsl$")          set(_stage "vs") set(_entry "VSMain")
  elseif(_rel MATCHES "(^|/)(ps[^/]*|[^/]*_ps)\\.hlsl$")          set(_stage "ps") set(_entry "PSMain")
  elseif(_rel MATCHES "(^|/)(ds[^/]*|[^/]*_ds|domain[^/]*).hlsl$")set(_stage "ds") set(_entry "DSMain")
  elseif(_rel MATCHES "(^|/)(hs[^/]*|[^/]*_hs|hull[^/]*).hlsl$")  set(_stage "hs") set(_entry "HSMain")
  elseif(_rel MATCHES "(^|/)(gs[^/]*|[^/]*_gs|geom[^/]*).hlsl$")  set(_stage "gs") set(_entry "GSMain")
  endif()
  set(${out_stage} "${_stage}" PARENT_SCOPE)
  set(${out_entry} "${_entry}" PARENT_SCOPE)
endfunction()

# --------------------------------------------------------------------------------------------------
# Initialize compilers and directories (multi-config friendly)
# --------------------------------------------------------------------------------------------------
function(cg_init_hlsl_compilers)
  set(options)
  set(oneValueArgs SHADER_DIR BIN_DIR)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "" ${ARGN})

  if(CG_SHADER_DIR)
    set(CG_SHADER_DIR "${CG_SHADER_DIR}" PARENT_SCOPE)
  else()
    set(CG_SHADER_DIR "${CMAKE_SOURCE_DIR}/res/shaders" PARENT_SCOPE)
  endif()

  # Per-config bin dir: use CMAKE_CFG_INTDIR for VS/Xcode multi-config.
  if(CG_BIN_DIR)
    set(_bin "${CG_BIN_DIR}")
  else()
    set(_bin "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
  endif()
  file(MAKE_DIRECTORY "${_bin}")
  set(CG_SHADER_BIN_DIR "${_bin}" PARENT_SCOPE)

  # Locate DXC/FXC
  find_program(DXC_BIN NAMES dxc dxc.exe
               HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin64" "$ENV{VULKAN_SDK}/Bin32"
                     "$ENV{DXC_DIR}" "$ENV{DXC_PATH}")
  if(DXC_BIN)
    message(STATUS "DXC found: ${DXC_BIN}")
    set(CG_DXC_FOUND TRUE PARENT_SCOPE)
    set(DXC_BIN "${DXC_BIN}" PARENT_SCOPE)
  else()
    set(CG_DXC_FOUND FALSE PARENT_SCOPE)
  endif()

  find_program(FXC_BIN fxc.exe)
  if(FXC_BIN)
    message(STATUS "FXC found: ${FXC_BIN}")
    set(CG_FXC_FOUND TRUE PARENT_SCOPE)
    set(FXC_BIN "${FXC_BIN}" PARENT_SCOPE)
  else()
    set(CG_FXC_FOUND FALSE PARENT_SCOPE)
  endif()

  if(NOT CG_DXC_FOUND AND NOT CG_FXC_FOUND)
    message(FATAL_ERROR "Neither DXC nor FXC found. Install Windows SDK (FXC) or provide DXC.")
  endif()
endfunction()

# --------------------------------------------------------------------------------------------------
# Target defaults (used when COMPILER/PROFILE omitted)
# --------------------------------------------------------------------------------------------------
function(cg_set_target_shader_defaults)
  set(options)
  set(oneValueArgs TARGET COMPILER DXC_SM FXC_SM)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "" ${ARGN})
  if(NOT CG_TARGET) message(FATAL_ERROR "TARGET is required") endif()

  if(NOT CG_COMPILER) set(CG_COMPILER "AUTO") endif()
  if(NOT CG_DXC_SM)  set(CG_DXC_SM  "${DXC_SHADER_MODEL}") endif()
  if(NOT CG_FXC_SM)  set(CG_FXC_SM  "${FXC_SHADER_MODEL}") endif()

  set_property(TARGET ${CG_TARGET} PROPERTY CG_SHADER_COMPILER_DEFAULT "${CG_COMPILER}")
  set_property(TARGET ${CG_TARGET} PROPERTY CG_DXC_SM_DEFAULT        "${CG_DXC_SM}")
  set_property(TARGET ${CG_TARGET} PROPERTY CG_FXC_SM_DEFAULT        "${CG_FXC_SM}")
endfunction()

# --- internal aggregation
function(_cg_append_shader_output tgt out)
  get_property(_curr TARGET ${tgt} PROPERTY CG_SHADER_OUTPUTS)
  if(NOT _curr) set(_curr "") endif()
  list(APPEND _curr "${out}")
  set_property(TARGET ${tgt} PROPERTY CG_SHADER_OUTPUTS "${_curr}")
endfunction()

# Global staged files aggregation
function(_cg_append_staged_output out)
  get_property(_curr GLOBAL PROPERTY CG_STAGED_FILES)
  if(NOT _curr) set(_curr "") endif()
  list(APPEND _curr "${out}")
  set_property(GLOBAL PROPERTY CG_STAGED_FILES "${_curr}")
endfunction()

# --------------------------------------------------------------------------------------------------
# Compile a single shader (adds Debug/Release flags; optional DXC PDB/reflection)
# --------------------------------------------------------------------------------------------------
function(cg_compile_hlsl)
  set(options)
  set(oneValueArgs TARGET SRC STAGE ENTRY OUT COMPILER PROFILE PDB REFLECTION)
  set(multiValueArgs DEFINES INCLUDES FLAGS_DXC FLAGS_FXC)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_TARGET OR NOT CG_SRC OR NOT CG_STAGE OR NOT CG_ENTRY OR NOT CG_OUT)
    message(FATAL_ERROR "cg_compile_hlsl: TARGET, SRC, STAGE, ENTRY, OUT are required")
  endif()

  if(NOT DEFINED CG_SHADER_DIR)
    set(CG_SHADER_DIR "${CMAKE_SOURCE_DIR}/res/shaders")
  endif()
  if(NOT DEFINED CG_SHADER_BIN_DIR)
    # default mirrors cg_init_hlsl_compilers
    set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
  endif()
  file(MAKE_DIRECTORY "${CG_SHADER_BIN_DIR}")
  get_filename_component(_OUT_DIR "${CG_OUT}" DIRECTORY)

  # Determine compiler choice
  get_property(_tgt_default TARGET ${CG_TARGET} PROPERTY CG_SHADER_COMPILER_DEFAULT)
  if(NOT _tgt_default) set(_tgt_default "AUTO") endif()
  if(NOT CG_COMPILER)  set(CG_COMPILER "${_tgt_default}") endif()

  if(NOT DEFINED CG_DXC_FOUND)
    find_program(DXC_BIN NAMES dxc dxc.exe)
    set(CG_DXC_FOUND ${DXC_BIN})
  endif()
  if(NOT DEFINED CG_FXC_FOUND)
    find_program(FXC_BIN fxc.exe)
    set(CG_FXC_FOUND ${FXC_BIN})
  endif()

  set(_use_dxc FALSE)
  if(CG_COMPILER STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(CG_COMPILER STREQUAL "FXC")
    set(_use_dxc FALSE)
  else()
    if(USE_DXC AND CG_DXC_FOUND)  # AUTO
      set(_use_dxc TRUE)
    else()
      set(_use_dxc FALSE)
    endif()
  endif()

  # Final profile
  if(NOT CG_PROFILE)
    if(_use_dxc)
      get_property(_sm TARGET ${CG_TARGET} PROPERTY CG_DXC_SM_DEFAULT)
      if(NOT _sm) set(_sm "${DXC_SHADER_MODEL}") endif()
      set(CG_PROFILE "${CG_STAGE}_${_sm}")
    else()
      get_property(_sm TARGET ${CG_TARGET} PROPERTY CG_FXC_SM_DEFAULT)
      if(NOT _sm) set(_sm "${FXC_SHADER_MODEL}") endif()
      set(CG_PROFILE "${CG_STAGE}_${_sm}")
    endif()
  endif()

  # Defines / includes
  set(_fxc_defs "") ; set(_dxc_defs "")
  foreach(D IN LISTS CG_DEFINES)
    list(APPEND _fxc_defs "/D" "${D}")
    list(APPEND _dxc_defs "-D${D}")
  endforeach()
  set(_fxc_inc "") ; set(_dxc_inc "")
  foreach(I IN LISTS CG_INCLUDES)
    list(APPEND _fxc_inc "/I" "${I}")
    list(APPEND _dxc_inc "-I" "${I}")
  endforeach()
  list(APPEND _fxc_inc "/I" "${CG_SHADER_DIR}")
  list(APPEND _dxc_inc "-I" "${CG_SHADER_DIR}")

  # Debug/Release flags (per-argument generator expressions; see notes)
  set(_dxc_cfg
    $<$<CONFIG:Debug>:-Od>
    $<$<CONFIG:Debug>:-Zi>
    $<$<CONFIG:Debug>:-Qembed_debug>
    $<$<NOT:$<CONFIG:Debug>>:-O3>
    $<$<NOT:$<CONFIG:Debug>>:-Qstrip_debug>
    $<$<NOT:$<CONFIG:Debug>>:-Qstrip_reflect>
  )
  set(_fxc_cfg
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/Zi>
    $<$<NOT:$<CONFIG:Debug>>:/O3>
    $<$<NOT:$<CONFIG:Debug>>:/Qstrip_debug>
    $<$<NOT:$<CONFIG:Debug>>:/Qstrip_reflect>
  )

  if(_use_dxc)
    if(NOT DXC_BIN) find_program(DXC_BIN NAMES dxc dxc.exe) endif()
    if(NOT DXC_BIN) message(FATAL_ERROR "DXC requested but not found") endif()
    add_custom_command(
      OUTPUT "${CG_OUT}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_OUT_DIR}"
      COMMAND "${DXC_BIN}"
              -nologo
              -T "${CG_PROFILE}"
              -E "${CG_ENTRY}"
              -Fo "${CG_OUT}"
              ${_dxc_cfg}
              $<$<BOOL:${CG_PDB}>:-Fd> $<$<BOOL:${CG_PDB}>:${CG_PDB}>
              $<$<BOOL:${CG_REFLECTION}>:-Fre> $<$<BOOL:${CG_REFLECTION}>:${CG_REFLECTION}>
              ${_dxc_defs} ${_dxc_inc} ${CG_FLAGS_DXC}
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      WORKING_DIRECTORY "${CG_SHADER_DIR}"
      COMMENT "DXC ${CG_SRC} (${CG_ENTRY}/${CG_PROFILE}) -> ${CG_OUT}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )
  else()
    if(NOT FXC_BIN) find_program(FXC_BIN fxc.exe) endif()
    if(NOT FXC_BIN) message(FATAL_ERROR "FXC requested but not found") endif()
    add_custom_command(
      OUTPUT "${CG_OUT}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_OUT_DIR}"
      COMMAND "${FXC_BIN}"
              /nologo
              /T "${CG_PROFILE}"
              /E "${CG_ENTRY}"
              /Fo "${CG_OUT}"
              ${_fxc_cfg}
              $<$<BOOL:${CG_PDB}>:/Fd> $<$<BOOL:${CG_PDB}>:${CG_PDB}>
              ${_fxc_defs} ${_fxc_inc} ${CG_FLAGS_FXC}
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      WORKING_DIRECTORY "${CG_SHADER_DIR}"
      COMMENT "FXC ${CG_SRC} (${CG_ENTRY}/${CG_PROFILE}) -> ${CG_OUT}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )
  endif()

  _cg_append_shader_output(${CG_TARGET} "${CG_OUT}")
endfunction()

# --------------------------------------------------------------------------------------------------
# Stage raw sources and expose a helper to retrieve them
# --------------------------------------------------------------------------------------------------
function(cg_stage_source SRC)
  if(NOT DEFINED CG_SHADER_BIN_DIR)
    set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
  endif()
  get_filename_component(_base "${SRC}" NAME)
  set(_dst "${CG_SHADER_BIN_DIR}/${_base}")
  add_custom_command(
    OUTPUT "${_dst}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CG_SHADER_BIN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${_dst}"
    DEPENDS "${SRC}"
    COMMENT "Staging shader source: ${_base}"
    VERBATIM
    COMMAND_EXPAND_LISTS
  )
  _cg_append_staged_output("${_dst}")
endfunction()

function(cg_finalize_staged_sources)
  get_property(_outs GLOBAL PROPERTY CG_STAGED_FILES)
  if(_outs)
    add_custom_target(cg_staged_sources ALL DEPENDS ${_outs})
  endif()
endfunction()

function(cg_list_staged_files OUTVAR)
  get_property(_outs GLOBAL PROPERTY CG_STAGED_FILES)
  set(${OUTVAR} "${_outs}" PARENT_SCOPE)
endfunction()

# --------------------------------------------------------------------------------------------------
# Finalize shaders for a target and copy binaries next to the EXE post-build
# --------------------------------------------------------------------------------------------------
function(cg_finalize_shader_target)
  set(options)
  set(oneValueArgs TARGET)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "" ${ARGN})
  if(NOT CG_TARGET) message(FATAL_ERROR "TARGET is required") endif()

  get_property(_outs TARGET ${CG_TARGET} PROPERTY CG_SHADER_OUTPUTS)
  if(_outs)
    add_custom_target(shaders_${CG_TARGET} ALL DEPENDS ${_outs})
    add_dependencies(${CG_TARGET} shaders_${CG_TARGET})

    if(NOT DEFINED CG_SHADER_BIN_DIR)
      set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
    endif()

    add_custom_command(TARGET ${CG_TARGET} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${CG_TARGET}>/shaders"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${CG_SHADER_BIN_DIR}" "$<TARGET_FILE_DIR:${CG_TARGET}>/shaders"
      COMMENT "Copying shaders to runtime dir for ${CG_TARGET}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )
  endif()
endfunction()

# ==================================================================================================
# JSON table + globs (same schema; supports extra stage synonyms)
# ==================================================================================================
function(cg_compile_hlsl_from_table)
  set(options)
  set(oneValueArgs TARGET TABLE)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "" ${ARGN})
  if(NOT CG_TARGET OR NOT CG_TABLE)
    message(FATAL_ERROR "cg_compile_hlsl_from_table: TARGET and TABLE are required")
  endif()
  if(NOT EXISTS "${CG_TABLE}")
    message(FATAL_ERROR "Shader table not found: ${CG_TABLE}")
  endif()
  file(READ "${CG_TABLE}" _json)

  # Defaults
  _json_try_get(_def_comp "${_json}" defaults compiler)
  _json_try_get(_def_dxcsm "${_json}" defaults dxc_sm)
  _json_try_get(_def_fxcsm "${_json}" defaults fxc_sm)

  set(_def_defines "") ; set(_def_includes "")
  _json_try_type(_ty_defs "${_json}" defaults defines)
  if(_ty_defs STREQUAL "ARRAY")
    _json_array_length(_n_defs "${_json}" defaults defines)
    if(_n_defs GREATER 0)
      math(EXPR _n_defs_m1 "${_n_defs}-1")
      foreach(i RANGE ${_n_defs_m1})
        _json_try_get(_d "${_json}" defaults defines ${i})
        if(_d) list(APPEND _def_defines "${_d}") endif()
      endforeach()
    endif()
  endif()
  _json_try_type(_ty_incs "${_json}" defaults includes)
  if(_ty_incs STREQUAL "ARRAY")
    _json_array_length(_n_incs "${_json}" defaults includes)
    if(_n_incs GREATER 0)
      math(EXPR _n_incs_m1 "${_n_incs}-1")
      foreach(i RANGE ${_n_incs_m1})
        _json_try_get(_inc "${_json}" defaults includes ${i})
        if(_inc) list(APPEND _def_includes "${_inc}") endif()
      endforeach()
    endif()
  endif()

  if(_def_comp OR _def_dxcsm OR _def_fxcsm)
    if(NOT _def_comp)  set(_def_comp "AUTO") endif()
    if(NOT _def_dxcsm) set(_def_dxcsm "${DXC_SHADER_MODEL}") endif()
    if(NOT _def_fxcsm) set(_def_fxcsm "${FXC_SHADER_MODEL}") endif()
    cg_set_target_shader_defaults(TARGET ${CG_TARGET}
      COMPILER "${_def_comp}" DXC_SM "${_def_dxcsm}" FXC_SM "${_def_fxcsm}")
  endif()

  # Track explicit sources
  set(_explicit_srcs "")

  # shaders[] (explicit)
  _json_try_type(_ty_sh "${_json}" shaders)
  if(_ty_sh STREQUAL "ARRAY")
    _json_array_length(_n_sh "${_json}" shaders)
    if(_n_sh GREATER 0)
      math(EXPR _n_sh_m1 "${_n_sh}-1")
      foreach(si RANGE ${_n_sh_m1})
        _json_try_get(_src "${_json}" shaders ${si} src)
        if(NOT _src) message(FATAL_ERROR "shaders[${si}] has no 'src'") endif()
        list(APPEND _explicit_srcs "${_src}")

        _json_try_type(_ty_stages "${_json}" shaders ${si} stages)
        if(NOT _ty_stages STREQUAL "ARRAY")
          message(FATAL_ERROR "shaders[${si}].stages must be an array")
        endif()
        _json_array_length(_n_stages "${_json}" shaders ${si} stages)
        if(_n_stages GREATER 0)
          math(EXPR _n_stages_m1 "${_n_stages}-1")
          foreach(ti RANGE ${_n_stages_m1})
            _json_try_get(_stage_in "${_json}" shaders ${si} stages ${ti} stage)
            _json_try_get(_entry   "${_json}" shaders ${si} stages ${ti} entry)
            _json_try_get(_prof    "${_json}" shaders ${si} stages ${ti} profile)
            _json_try_get(_comp    "${_json}" shaders ${si} stages ${ti} compiler)
            _json_try_get(_outname "${_json}" shaders ${si} stages ${ti} out)

            set(_stage "")
            if(_stage_in) _norm_stage("${_stage_in}" _stage) endif()

            set(_defs "${_def_defines}") ; set(_incs "${_def_includes}")
            set(_flg_dxc "") ; set(_flg_fxc "")

            foreach(_k IN ITEMS defines includes flags_dxc flags_fxc)
              _json_try_type(_ty "${_json}" shaders ${si} stages ${ti} ${_k})
              if(_ty STREQUAL "ARRAY")
                _json_array_length(_m "${_json}" shaders ${si} stages ${ti} ${_k})
                if(_m GREATER 0)
                  math(EXPR _mm "${_m}-1")
                  foreach(k RANGE ${_mm})
                    _json_try_get(__v "${_json}" shaders ${si} stages ${ti} ${_k} ${k})
                    if(__v)
                      if(_k STREQUAL "defines")    list(APPEND _defs    "${__v}")
                      elseif(_k STREQUAL "includes")list(APPEND _incs    "${__v}")
                      elseif(_k STREQUAL "flags_dxc")list(APPEND _flg_dxc "${__v}")
                      elseif(_k STREQUAL "flags_fxc")list(APPEND _flg_fxc "${__v}")
                      endif()
                    endif()
                  endforeach()
                endif()
              endif()
            endforeach()

            if(NOT _outname)
              get_filename_component(_stem "${_src}" NAME_WE)
              _stage_suffix(_suf "${_stage}")
              if(NOT _suf) set(_suf "_UNK") endif()
              set(_outname "${_stem}${_suf}.cso")
            endif()

            set(_OUT "${CG_SHADER_BIN_DIR}/${_outname}")
            get_filename_component(_OUT_DIR "${_OUT}" DIRECTORY)
            file(MAKE_DIRECTORY "${_OUT_DIR}")

            cg_compile_hlsl(
              TARGET   ${CG_TARGET}
              SRC      "${_src}"
              STAGE    "${_stage}"
              ENTRY    "${_entry}"
              OUT      "${_OUT}"
              COMPILER "${_comp}"
              PROFILE  "${_prof}"
              DEFINES  ${_defs}
              INCLUDES ${_incs}
              FLAGS_DXC ${_flg_dxc}
              FLAGS_FXC ${_flg_fxc}
            )
          endforeach()
        endif()
      endforeach()
    endif()
  endif()

  # globs[]
  _json_try_type(_ty_gl "${_json}" globs)
  if(_ty_gl STREQUAL "ARRAY")
    _json_array_length(_n_gl "${_json}" globs)
    if(_n_gl GREATER 0)
      math(EXPR _n_gl_m1 "${_n_gl}-1")
      foreach(gi RANGE ${_n_gl_m1})
        # patterns
        set(_patterns "")
        _json_try_type(_pty "${_json}" globs ${gi} patterns)
        if(_pty STREQUAL "ARRAY")
          _json_array_length(_np "${_json}" globs ${gi} patterns)
          if(_np GREATER 0)
            math(EXPR _np_m1 "${_np}-1")
            foreach(pi RANGE ${_np_m1})
              _json_try_get(_pat "${_json}" globs ${gi} patterns ${pi})
              if(_pat) list(APPEND _patterns "${_pat}") endif()
            endforeach()
          endif()
        else()
          _json_try_get(_singlep "${_json}" globs ${gi} pattern)
          if(_singlep) list(APPEND _patterns "${_singlep}") endif()
        endif()
        if(NOT _patterns)
          message(WARNING "globs[${gi}] has no patterns; skipping")
          continue()
        endif()

        # excludes
        set(_ex_re "")
        _json_try_type(_xty "${_json}" globs ${gi} exclude_regex)
        if(_xty STREQUAL "ARRAY")
          _json_array_length(_nx "${_json}" globs ${gi} exclude_regex)
          if(_nx GREATER 0)
            math(EXPR _nx_m1 "${_nx}-1")
            foreach(xi RANGE ${_nx_m1})
              _json_try_get(_rx "${_json}" globs ${gi} exclude_regex ${xi})
              if(_rx) list(APPEND _ex_re "${_rx}") endif()
            endforeach()
          endif()
        endif()

        # assume
        set(_g_comp "")
        set(_g_defs "${_def_defines}")
        set(_g_incs "${_def_includes}")
        _json_try_get(_g_comp "${_json}" globs ${gi} assume compiler)

        foreach(_k IN ITEMS defines includes)
          _json_try_type(_gty "${_json}" globs ${gi} assume ${_k})
          if(_gty STREQUAL "ARRAY")
            _json_array_length(_gl "${_json}" globs ${gi} assume ${_k})
            if(_gl GREATER 0)
              math(EXPR _gl_m1 "${_gl}-1")
              foreach(di RANGE ${_gl_m1})
                _json_try_get(_v "${_json}" globs ${gi} assume ${_k} ${di})
                if(_v)
                  if(_k STREQUAL "defines") list(APPEND _g_defs "${_v}") else() list(APPEND _g_incs "${_v}") endif()
                endif()
              endforeach()
            endif()
          endif()
        endforeach()

        # map rules
        set(_have_map FALSE)
        _json_try_type(_mty "${_json}" globs ${gi} map)
        if(_mty STREQUAL "ARRAY")
          _json_array_length(_nm "${_json}" globs ${gi} map)
          if(_nm GREATER 0) set(_have_map TRUE) endif()
        endif()

        # gather files
        set(_matches "")
        foreach(_pat IN LISTS _patterns)
          file(GLOB_RECURSE _gfiles RELATIVE "${CG_SHADER_DIR}" "${CG_SHADER_DIR}/${_pat}")
          list(APPEND _matches ${_gfiles})
        endforeach()
        list(REMOVE_DUPLICATES _matches)

        foreach(rel IN LISTS _matches)
          # skip explicit (best-effort)
          list(FIND _explicit_srcs "${rel}" _idx)
          if(NOT _idx EQUAL -1) continue() endif()

          # excludes
          set(_skip FALSE)
          foreach(rx IN LISTS _ex_re)
            if("${rel}" MATCHES "${rx}") set(_skip TRUE) break() endif()
          endforeach()
          if(_skip) continue() endif()

          # decide mapping
          set(_stage "") ; set(_entry "") ; set(_prof "") ; set(_comp "${_g_comp}")

          if(_have_map)
            _json_array_length(_nm2 "${_json}" globs ${gi} map)
            if(_nm2 GREATER 0)
              math(EXPR _nm2_m1 "${_nm2}-1")
              foreach(mi RANGE ${_nm2_m1})
                _json_try_get(_rx "${_json}" globs ${gi} map ${mi} regex)
                if(_rx AND "${rel}" MATCHES "${_rx}")
                  _json_try_get(_s "${_json}" globs ${gi} map ${mi} stage)
                  if(_s) _norm_stage("${_s}" _stage) endif()
                  _json_try_get(_e "${_json}" globs ${gi} map ${mi} entry)
                  _json_try_get(_p "${_json}" globs ${gi} map ${mi} profile)
                  _json_try_get(_c "${_json}" globs ${gi} map ${mi} compiler)
                  if(_c) set(_comp "${_c}") endif()
                  break()
                endif()
              endforeach()
            endif()
          endif()

          if(NOT _stage)
            _infer_stage_and_entry("${rel}" _stage _entry)
          endif()
          if(NOT _stage OR NOT _entry)
            message(WARNING "Could not infer stage/entry for '${rel}'; skipping. Add a map rule.")
            continue()
          endif()

          # output
          get_filename_component(_dir  "${rel}" DIRECTORY)
          get_filename_component(_stem "${rel}" NAME_WE)
          _stage_suffix(_suf "${_stage}")
          if(NOT _suf) set(_suf "_UNK") endif()
          if(_dir)
            file(MAKE_DIRECTORY "${CG_SHADER_BIN_DIR}/${_dir}")
            set(_OUT "${CG_SHADER_BIN_DIR}/${_dir}/${_stem}${_suf}.cso")
          else()
            set(_OUT "${CG_SHADER_BIN_DIR}/${_stem}${_suf}.cso")
          endif()

          cg_compile_hlsl(
            TARGET   ${CG_TARGET}
            SRC      "${rel}"
            STAGE    "${_stage}"
            ENTRY    "${_entry}"
            OUT      "${_OUT}"
            COMPILER "${_comp}"
            PROFILE  "${_prof}"
            DEFINES  ${_g_defs}
            INCLUDES ${_g_incs}
          )
        endforeach()
      endforeach()
    endif()
  endif()
endfunction()

