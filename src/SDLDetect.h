#pragma once
#if __has_include(<SDL.h>)
  #include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
  #include <SDL2/SDL.h>
#else
  #error "SDL2 headers not found. Install SDL2 dev headers or build with -DBUILD_WITH_SDL2=OFF."
#endif
