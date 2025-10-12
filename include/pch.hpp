#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX               // prevents <Windows.h> min/max macros
#endif
#ifndef UNICODE
#  define UNICODE
#  define _UNICODE
#endif

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
