# cmake/CGToolchainWin.cmake
#
# Windows toolchain wiring:
# - Central MSVC/clang-cl default warnings & conformance flags
# - Sanitizer toggles (ASan via MSVC or Clang)
# - Optional link-warnings-as-errors (/WX) for executables
#
# NOTE:
# - MSVC ASan is enabled via /fsanitize=address (compiler option). The runtime libs
#   are inferred/linked automatically unless you suppress default libs (advanced use).
#
# Call:
#   cg_toolchain_win_setup_target(MyTarget WERROR ON|OFF IS_EXE ON|OFF LINK_WERROR ON|OFF)

include_guard(GLOBAL)

# ------------------------------ Toolchain toggles ------------------------------

option(COLONY_ENABLE_ASAN
  "Enable AddressSanitizer (MSVC: /fsanitize=address, Clang: -fsanitize=address)"
  OFF
)
option(COLONY_ENABLE_UBSAN
  "Enable UndefinedBehaviorSanitizer (Clang only on Windows)"
  OFF
)
option(COLONY_ENABLE_TSAN
  "Enable ThreadSanitizer (Clang only; generally unavailable with MSVC toolchains)"
  OFF
)

option(COLONY_MSVC_LINK_WERROR
  "Treat linker warnings as errors (/WX) for executables (MSVC only)"
  ON
)

# ------------------------------ API ------------------------------

function(cg_toolchain_win_setup_target target)
  cmake_parse_arguments(ARG "" "WERROR;IS_EXE;LINK_WERROR" "" ${ARGN})

  if(NOT TARGET "${target}")
    message(FATAL_ERROR "cg_toolchain_win_setup_target: '${target}' is not a CMake target.")
  endif()

  # ---- Defaults / warnings ----
  #
  # We treat clang-cl as "MSVC" here because it accepts most MSVC flags
  # and usually participates in MSVC-like builds.
  if(MSVC)
    target_compile_options("${target}" PRIVATE
      /permissive-
      /Zc:preprocessor
      /Zc:__cplusplus
      /utf-8
      /W4
      /MP
    )
    if(ARG_WERROR)
      target_compile_options("${target}" PRIVATE /WX)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # For non-MSVC Clang on Windows (rare, but safe)
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    if(ARG_WERROR)
      target_compile_options("${target}" PRIVATE -Werror)
    endif()
  endif()

  # ---- Sanitizers (compile) ----
  if(COLONY_ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options("${target}" PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    elseif(MSVC)
      target_compile_options("${target}" PRIVATE /fsanitize=address)
    endif()
  endif()

  if(COLONY_ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options("${target}" PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    else()
      message(WARNING "[Toolchain] UBSan requested but only wired for Clang on Windows; ignoring for '${target}'.")
    endif()
  endif()

  if(COLONY_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options("${target}" PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    else()
      message(WARNING "[Toolchain] TSan requested but not supported with MSVC; ignoring for '${target}'.")
    endif()
  endif()

  # ---- Sanitizers (link) ----
  #
  # Clang generally needs -fsanitize=... at link time. MSVC ASan is driven by
  # the compiler flag and inferred libs; we don't push a /fsanitize flag to link.exe.
  if(ARG_IS_EXE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      if(COLONY_ENABLE_ASAN)
        target_link_options("${target}" PRIVATE -fsanitize=address)
      endif()
      if(COLONY_ENABLE_UBSAN)
        target_link_options("${target}" PRIVATE -fsanitize=undefined)
      endif()
      if(COLONY_ENABLE_TSAN)
        target_link_options("${target}" PRIVATE -fsanitize=thread)
      endif()
    endif()

    # ---- Link warnings as errors (/WX) ----
    set(_link_werror "${COLONY_MSVC_LINK_WERROR}")
    if(NOT "${ARG_LINK_WERROR}" STREQUAL "")
      set(_link_werror "${ARG_LINK_WERROR}")
    endif()

    if(_link_werror AND MSVC)
      target_link_options("${target}" PRIVATE /WX)
    endif()
    unset(_link_werror)
  endif()
endfunction()
