if(MSVC)
  # /permissive-, correct __cplusplus, conformance
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:throwingNew /utf-8)
  add_compile_options(/MP)                   # parallel compilation
  add_compile_options($<$<CONFIG:Release>:/O2 /Gw /Gy /GL>)
  add_link_options($<$<CONFIG:Release>:/LTCG>)

  if(MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()

  # Warnings
  add_compile_options(/W4)
  if(WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
