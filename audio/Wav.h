#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <windows.h>
#include <mmreg.h>        // WAVEFORMATEX / WAVEFORMATEXTENSIBLE

namespace wav {

// Holds format + data start for streaming.
struct WavInfo {
    WAVEFORMATEX*  wfx = nullptr; // allocated via new[]; free with delete[]
    uint64_t       dataOffset = 0;
    uint64_t       dataBytes  = 0;
};

// Minimal RIFF/WAVE reader supporting PCM (0x0001), IEEE float (0x0003) and EXTENSIBLE (0xFFFE).
// Throws std::runtime_error on parse errors.
WavInfo ReadHeader(const std::wstring& path);

// Load entire .wav file data into memory (for SFX).
// Returns (format, raw bytes). Throws on error.
std::pair<std::unique_ptr<WAVEFORMATEX[], void(*)(void*)>, std::vector<uint8_t>>
LoadWholeFile(const std::wstring& path);

// Helper: compute bytes per frame (all channels).
inline uint32_t BytesPerFrame(const WAVEFORMATEX& wfx) { return wfx.nBlockAlign; }

} // namespace wav
