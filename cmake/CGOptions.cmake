# cmake/CGOptions.cmake
cmake_minimum_required(VERSION 3.24)

# Ensure CMP0091 NEW so MSVC runtime selection via CMAKE_MSVC_RUNTIME_LIBRARY works.  (CMake 3.15+)
# Ref: CMP0091 and MSVC runtime docs
# https://cmake.org/cmake/help/latest/policy/CMP0091.html
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

option(CG_ENABLE_PCH "Enable precompiled headers" ON)
option(CG_ENABLE_UNITY "Enable unity/jumbo builds" OFF)

if(CG_ENABLE_UNITY)
  set(CMAKE_UNITY_BUILD ON)
  # Reasonable batch size for medium projects
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
endif()

function(cg_apply_defaults_to_target tgt)
  if (NOT TARGET ${tgt})
    message(FATAL_ERROR "cg_apply_defaults_to_target: target '${tgt}' doesn't exist")
  endif()

  # Output dirs
  set_target_properties(${tgt} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  if (MSVC)
    target_compile_options(${tgt} PRIVATE
      /W4 /permissive- /EHsc /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zi /MP /utf-8
      /Gw /Gy
    )
    target_compile_definitions(${tgt} PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    )
    # Security/link flags
    target_link_options(${tgt} PRIVATE
      /DYNAMICBASE /NXCOMPAT /guard:cf
      $<$<CONFIG:Release>:/INCREMENTAL:NO>
      $<$<CONFIG:RelWithDebInfo>:/DEBUG:FASTLINK>
    )
  endif()

  if (CG_ENABLE_PCH)
    # Use a lightweight PCH to speed up Windows headers + STL
    target_precompile_headers(${tgt} PRIVATE "${CMAKE_SOURCE_DIR}/include/CG/pch.hpp")
  endif()

  if (CG_ENABLE_UNITY)
    set_target_properties(${tgt} PROPERTIES UNITY_BUILD ON)
  endif()
endfunction()

# Copy common asset directories next to the built exe
function(cg_install_assets tgt)
  foreach(dir assets res resources shaders audio data)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}")
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/${dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/${dir}" "$<TARGET_FILE_DIR:${tgt}>/${dir}"
        COMMENT "Copying ${dir} -> $<TARGET_FILE_DIR:${tgt}>/${dir}"
        VERBATIM
      )
    endif()
  endforeach()
endfunction()
