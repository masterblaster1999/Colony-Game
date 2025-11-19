// src/gfx/UnusedMacro.h
// Helper macro to explicitly mark variables/parameters as intentionally unused.
// This is used to silence MSVC C4100 ("unreferenced formal parameter") when we
// *intentionally* don't use an argument.

#pragma once

#ifndef UNUSED
#   define UNUSED(x) (void)(x)
#endif
