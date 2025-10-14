// include/CG/pch.hpp
#pragma once
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <Windows.h>
  #include <Shlobj.h>      // SHGetKnownFolderPath
  #include <KnownFolders.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
