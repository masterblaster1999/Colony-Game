#pragma once
#include <cstdint>
struct IXAudio2; struct IXAudio2MasteringVoice;

namespace audio {
bool Init();            // once at startup
void Shutdown();        // on exit
bool IsReady();         // engine+device ok
// (extend with play/stop APIs as needed)
}
