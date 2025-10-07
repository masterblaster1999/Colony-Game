// src/core/Profile.h
#pragma once

// Tracy (optional)
#if __has_include(<tracy/Tracy.hpp>)
  #include <tracy/Tracy.hpp>
  #define CG_TRACY 1
#else
  #define CG_TRACY 0
#endif

#if CG_TRACY
  #define CG_ZONE(name_literal)        ZoneScopedN(name_literal)
  #define CG_FRAME_MARK()              FrameMark
  #define CG_PLOT(name_literal, val)   TracyPlot(name_literal, val)
#else
  #define CG_ZONE(name_literal)        do{}while(0)
  #define CG_FRAME_MARK()              do{}while(0)
  #define CG_PLOT(name_literal, val)   do{}while(0)
#endif
