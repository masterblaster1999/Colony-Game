// src/common/Compiler.h
//
// Centralised compiler / platform helpers for Colony-Game.
//
// - Detection macros (compiler, platform, build config)
// - Small utility macros (UNUSED, FORCE_INLINE, DEBUG_BREAK, etc.)
// - All prefixed with COLONY_ to avoid collisions.
//
// This header is intentionally lightweight and has no external dependencies
// beyond the standard library. It is safe to include almost everywhere.

#pragma once

// -------------------------------------------------------------------------------------------------
//  Compiler detection
// -------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
    #ifndef COLONY_COMPILER_MSVC
        #define COLONY_COMPILER_MSVC 1
    #endif
#else
    #ifndef COLONY_COMPILER_MSVC
        #define COLONY_COMPILER_MSVC 0
    #endif
#endif

#if defined(__clang__)
    #ifndef COLONY_COMPILER_CLANG
        #define COLONY_COMPILER_CLANG 1
    #endif
#else
    #ifndef COLONY_COMPILER_CLANG
        #define COLONY_COMPILER_CLANG 0
    #endif
#endif

#if defined(__GNUC__) && !defined(__clang__)
    #ifndef COLONY_COMPILER_GCC
        #define COLONY_COMPILER_GCC 1
    #endif
#else
    #ifndef COLONY_COMPILER_GCC
        #define COLONY_COMPILER_GCC 0
    #endif
#endif

// -------------------------------------------------------------------------------------------------
//  Platform detection
// -------------------------------------------------------------------------------------------------

#if defined(_WIN32) || defined(_WIN64)
    #ifndef COLONY_PLATFORM_WINDOWS
        #define COLONY_PLATFORM_WINDOWS 1
    #endif
#else
    #ifndef COLONY_PLATFORM_WINDOWS
        #define COLONY_PLATFORM_WINDOWS 0
    #endif
#endif

// You said this project is Windows-only, so it’s OK to rely on this in your code.
// (We don’t hard-error here so that tools / static analyzers on other platforms still work.)

// -------------------------------------------------------------------------------------------------
//  Build configuration (debug / release)
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_DEBUG
    #if !defined(NDEBUG)
        #define COLONY_DEBUG 1
    #else
        #define COLONY_DEBUG 0
    #endif
#endif

#ifndef COLONY_RELEASE
    #define COLONY_RELEASE (!COLONY_DEBUG)
#endif

// -------------------------------------------------------------------------------------------------
//  Helper macros: stringize / concatenate
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_STRINGIZE
    #define COLONY_STRINGIZE_DETAIL(x) #x
    #define COLONY_STRINGIZE(x) COLONY_STRINGIZE_DETAIL(x)
#endif

#ifndef COLONY_CONCAT
    #define COLONY_CONCAT_DETAIL(a, b) a##b
    #define COLONY_CONCAT(a, b) COLONY_CONCAT_DETAIL(a, b)
#endif

// -------------------------------------------------------------------------------------------------
//  UNUSED — fixes your C4100 warnings (parameters / variables)
// -------------------------------------------------------------------------------------------------

// Use COLONY_UNUSED(x) for parameters or local variables that are intentionally unused.
// Example:
//    void Foo(int x) { COLONY_UNUSED(x); /* ... */ }

#ifndef COLONY_UNUSED
    #define COLONY_UNUSED(x) (void)(x)
#endif

// -------------------------------------------------------------------------------------------------
//  Force inline / noinline
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_FORCE_INLINE
    #if COLONY_COMPILER_MSVC
        #define COLONY_FORCE_INLINE __forceinline
    #elif COLONY_COMPILER_GCC || COLONY_COMPILER_CLANG
        #define COLONY_FORCE_INLINE inline __attribute__((always_inline))
    #else
        #define COLONY_FORCE_INLINE inline
    #endif
#endif

#ifndef COLONY_NOINLINE
    #if COLONY_COMPILER_MSVC
        #define COLONY_NOINLINE __declspec(noinline)
    #elif COLONY_COMPILER_GCC || COLONY_COMPILER_CLANG
        #define COLONY_NOINLINE __attribute__((noinline))
    #else
        #define COLONY_NOINLINE
    #endif
#endif

// -------------------------------------------------------------------------------------------------
//  Debug break
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_DEBUG_BREAK
    #if COLONY_COMPILER_MSVC
        #define COLONY_DEBUG_BREAK() __debugbreak()
    #elif COLONY_COMPILER_GCC || COLONY_COMPILER_CLANG
        #include <signal.h>
        #define COLONY_DEBUG_BREAK() ::raise(SIGTRAP)
    #else
        #define COLONY_DEBUG_BREAK() ((void)0)
    #endif
#endif

// -------------------------------------------------------------------------------------------------
//  Assertions (optional, lightweight)
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_ASSERT
    #if COLONY_DEBUG
        #include <cassert>
        #define COLONY_ASSERT(expr) \
            do {                     \
                if (!(expr)) {       \
                    COLONY_DEBUG_BREAK(); \
                    assert(expr);    \
                }                    \
            } while (false)
    #else
        #define COLONY_ASSERT(expr) ((void)0)
    #endif
#endif

// -------------------------------------------------------------------------------------------------
//  Assumptions / unreachable
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_ASSUME
    #if COLONY_COMPILER_MSVC
        #define COLONY_ASSUME(expr) __assume(expr)
    #elif COLONY_COMPILER_GCC || COLONY_COMPILER_CLANG
        #define COLONY_ASSUME(expr) \
            do { if (!(expr)) __builtin_unreachable(); } while (false)
    #else
        #define COLONY_ASSUME(expr) ((void)0)
    #endif
#endif

#ifndef COLONY_UNREACHABLE
    #define COLONY_UNREACHABLE() COLONY_ASSUME(false)
#endif

// -------------------------------------------------------------------------------------------------
//  NODISCARD convenience (C++17+)
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_NODISCARD
    #if defined(__has_cpp_attribute)
        #if __has_cpp_attribute(nodiscard)
            #define COLONY_NODISCARD [[nodiscard]]
        #else
            #define COLONY_NODISCARD
        #endif
    #else
        #define COLONY_NODISCARD
    #endif
#endif

// -------------------------------------------------------------------------------------------------
//  End of Compiler.h
// -------------------------------------------------------------------------------------------------
