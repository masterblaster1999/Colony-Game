#pragma once
#include <windows.h>
namespace crashwin {
  void install_minidump_writer(const wchar_t* gameName); // e.g. L"ColonyGame"
}
