#define WIN32_LEAN_AND_MEAN
#include "XAudio2Engine.h"
#include <xaudio2.h>
#include <wrl/client.h>
#include <atomic>
#pragma comment(lib, "xaudio2.lib")
using Microsoft::WRL::ComPtr;

namespace {
ComPtr<IXAudio2> g_xa;
IXAudio2MasteringVoice* g_master = nullptr;
std::atomic<bool> g_ready{false};
}

namespace audio {
bool Init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // CoInitializeEx may return RPC_E_CHANGED_MODE if already initialized; ignore

    if (FAILED(XAudio2Create(g_xa.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        g_ready = false; return false;
    }
    if (FAILED(g_xa->CreateMasteringVoice(&g_master))) {
        g_xa.Reset(); g_ready = false; return false;
    }
    g_ready = true;
    return true;
}

void Shutdown() {
    if (g_master) { g_master->DestroyVoice(); g_master = nullptr; }
    g_xa.Reset();
    g_ready = false;
    CoUninitialize();
}

bool IsReady() { return g_ready.load(); }
}
