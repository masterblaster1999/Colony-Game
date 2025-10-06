#pragma once
#include <objbase.h>

struct ComInit {
  HRESULT hr{E_FAIL};
  explicit ComInit(DWORD coinit = COINIT_MULTITHREADED) noexcept {
    hr = CoInitializeEx(nullptr, coinit);
  }
  ~ComInit() {
    if (SUCCEEDED(hr)) CoUninitialize();
  }
  ComInit(const ComInit&) = delete;
  ComInit& operator=(const ComInit&) = delete;
};
