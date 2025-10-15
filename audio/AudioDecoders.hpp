// audio/AudioDecoders.hpp
#pragma once
#include <string>
#include <vector>
#include <mmreg.h>    // WAVEFORMATEX/WAVEFORMATEXTENSIBLE

// Fill wfx (WAVEFORMATEXTENSIBLE), interleaved PCM bytes, and frame count.
bool DecodeFileToPCM(const std::wstring& path,
                     WAVEFORMATEXTENSIBLE& outWfx,
                     std::vector<uint8_t>& outBytes,
                     uint32_t& outFrames);
