#pragma once
namespace audio {

class AudioSystem {
public:
    bool Init() { return true; }
    void Shutdown() {}
    void PlayTestBeep() {} // TODO
};

} // namespace audio
