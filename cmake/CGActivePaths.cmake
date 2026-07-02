include_guard(GLOBAL)

option(
  COLONY_STRICT_ACTIVE_PATHS
  "Fail configure when default targets include sources from inactive subsystem families."
  ON
)

function(_colony_active_paths_relpath out_var source_path)
  if(IS_ABSOLUTE "${source_path}")
    set(_abs "${source_path}")
  else()
    get_filename_component(_abs "${source_path}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  endif()

  file(TO_CMAKE_PATH "${_abs}" _abs_norm)
  file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _root_norm)
  file(RELATIVE_PATH _rel "${_root_norm}" "${_abs_norm}")
  file(TO_CMAKE_PATH "${_rel}" _rel_norm)
  set(${out_var} "${_rel_norm}" PARENT_SCOPE)
endfunction()

function(_colony_active_paths_reason out_var rel_path)
  set(_reason "")

  if(rel_path MATCHES "^src/render/")
    set(_reason "legacy renderer family; use renderer/ and colony::world_render")
  elseif(rel_path MATCHES "^src/renderer/" AND NOT rel_path MATCHES "^src/renderer/RenderGraph\\.(cpp|h)$")
    set(_reason "legacy renderer family; only src/renderer/RenderGraph.* is active")
  elseif(rel_path MATCHES "^platform/win/")
    set(_reason "legacy Windows platform tree; use src/platform/win")
  elseif(rel_path MATCHES "^src/platform/win32/")
    set(_reason "legacy Win32 app shim tree; use src/platform/win")
  elseif(rel_path MATCHES "^src/nav/")
    set(_reason "legacy navigation stack; use PathService and colony::nav")
  elseif(rel_path MATCHES "^src/navigation/")
    set(_reason "experimental navigation stack; use PathService and colony::nav")
  elseif(rel_path MATCHES "^src/pathfinding/")
    set(_reason "legacy pathfinding API; use PathService and colony::nav")
  elseif(rel_path MATCHES "^hpa/")
    set(_reason "legacy HPA navigation stack; use PathService and colony::nav")
  elseif(rel_path MATCHES "^pathfinding/procgen/")
    set(_reason "pathfinding procgen experiment; keep out of the runtime nav target")
  elseif(rel_path MATCHES "^experiments/render/")
    set(_reason "renderer experiment; keep out of default runtime targets")
  elseif(rel_path MATCHES "^experiments/platform/")
    set(_reason "platform experiment; keep out of default runtime targets")
  elseif(rel_path MATCHES "^experiments/navigation/")
    set(_reason "navigation experiment; keep out of default runtime targets")
  endif()

  set(${out_var} "${_reason}" PARENT_SCOPE)
endfunction()

function(colony_assert_active_runtime_sources label sources_var)
  if(NOT COLONY_STRICT_ACTIVE_PATHS)
    return()
  endif()

  set(_bad "")
  foreach(_src IN LISTS ${sources_var})
    if(_src MATCHES "^\\$<")
      continue()
    endif()

    _colony_active_paths_relpath(_rel "${_src}")
    _colony_active_paths_reason(_reason "${_rel}")
    if(_reason)
      list(APPEND _bad "  ${_rel} (${_reason})")
    endif()
  endforeach()

  if(_bad)
    list(JOIN _bad "\n" _bad_text)
    message(FATAL_ERROR
      "${label} includes inactive subsystem sources while COLONY_STRICT_ACTIVE_PATHS=ON.\n"
      "See ARCHITECTURE_ACTIVE_PATHS.md.\n"
      "${_bad_text}"
    )
  endif()
endfunction()
