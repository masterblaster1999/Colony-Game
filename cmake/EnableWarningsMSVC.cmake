function(enable_msvc_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /permissive- /W4 /wd4201 /wd4324)
    # Optional: treat warnings as errors for your code:
    # target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
  endif()
endfunction()
