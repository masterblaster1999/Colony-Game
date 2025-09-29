# cmake/msvc_flags.cmake
if(MSVC)
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:throwingNew /utf-8 /MP)
  # Release tuning; keep LTO/IPO under your COLONY_LTO gate from the top-level CMake
  add_compile_options($<$<CONFIG:Release>:/O2 /Gw /Gy>)
  add_link_options($<$<CONFIG:Release>:/INCREMENTAL:NO>)
  if(WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
endif()
