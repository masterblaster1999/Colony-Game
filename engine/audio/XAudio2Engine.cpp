// engine/audio/XAudio2Engine.cpp
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include <wrl/client.h>
#include <xaudio2.h>

#include "XAudio2Engine.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
Microsoft::WRL::ComPtr<IXAudio2> g_xaudio;
IXAudio2MasteringVoice*          g_masterVoice = nullptr;

std::atomic_bool g_ready{false};
bool             g_shouldUninitCOM = false;
} // namespace

namespace audio
{
bool Init()
{
    if (g_ready.load(std::memory_order_acquire))
        return true;

    // COM init (XAudio2 on Windows uses COM internally).
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
    {
        // S_OK or S_FALSE: we must pair with CoUninitialize.
        g_shouldUninitCOM = true;
    }
    else if (hr == RPC_E_CHANGED_MODE)
    {
        // COM already initialized with a different concurrency model on this thread.
        // We can still proceed, but must NOT call CoUninitialize().
        g_shouldUninitCOM = false;
    }
    else
    {
        g_ready.store(false, std::memory_order_release);
        return false;
    }

    hr = ::XAudio2Create(g_xaudio.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        g_xaudio.Reset();
        if (g_shouldUninitCOM)
        {
            ::CoUninitialize();
            g_shouldUninitCOM = false;
        }
        g_ready.store(false, std::memory_order_release);
        return false;
    }

    hr = g_xaudio->CreateMasteringVoice(&g_masterVoice);
    if (FAILED(hr))
    {
        g_xaudio.Reset();
        if (g_shouldUninitCOM)
        {
            ::CoUninitialize();
            g_shouldUninitCOM = false;
        }
        g_ready.store(false, std::memory_order_release);
        return false;
    }

    g_ready.store(true, std::memory_order_release);
    return true;
}

void Shutdown()
{
    // Tear down voices first.
    if (g_masterVoice)
    {
        g_masterVoice->DestroyVoice();
        g_masterVoice = nullptr;
    }

    g_xaudio.Reset();
    g_ready.store(false, std::memory_order_release);

    if (g_shouldUninitCOM)
    {
        ::CoUninitialize();
        g_shouldUninitCOM = false;
    }
}

bool IsReady()
{
    return g_ready.load(std::memory_order_acquire);
}
} // namespace audio
