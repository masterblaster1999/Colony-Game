# cmake/ColonyCommonOptions.cmake
include(CheckCXXCompilerFlag)
if(MSVC)
  add_compile_options(
    /permissive-          # Stricter C++ conformance
    /W4                   # High warnings
    /w44265 /w44062       # Narrowing & signed/unsigned conversions as warnings
    /Zc:__cplusplus       # Proper __cplusplus
    /Zc:preprocessor      # Standard preprocessor
    /EHsc                 # Exceptions model
    /MP                   # Multi-proc compilation
  )
  # Optional: flip on AddressSanitizer for Debug (available in VS 17.7+)
  # add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
  # link options for /fsanitize appear automatically in recent VS.
endif()

# Turn warnings into errors in CI only
if(DEFINED ENV{CI})
  add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

# Precompiled headers support (example target)
# target_precompile_headers(Colony PRIVATE src/pch.hpp)
