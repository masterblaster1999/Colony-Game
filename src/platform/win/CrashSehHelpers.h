*** a/src/platform/win/CrashSehHelpers.h  (new)
+ #pragma once
+ #include <windows.h>
+ 
+ using VoidFunc = void(__cdecl*)();
+ 
+ // Put SEH in a function with no locals requiring unwinding.
+ inline void SehInvokeNoUnwind(VoidFunc fn, LONG (__stdcall* filter)(EXCEPTION_POINTERS*)) {
+   __try {
+     fn();
+   } __except(filter(GetExceptionInformation())) {
+     // swallow: the filter is expected to log/dump
+   }
+ }
