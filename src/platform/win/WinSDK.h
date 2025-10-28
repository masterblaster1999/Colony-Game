+ #pragma once
+ 
+ // Keep Windows headers lean and avoid macro min/max pollution.
+ #ifndef WIN32_LEAN_AND_MEAN
+ #  define WIN32_LEAN_AND_MEAN 1
+ #endif
+ #ifndef NOMINMAX
+ #  define NOMINMAX 1
+ #endif
+ 
+ #include <Windows.h>
