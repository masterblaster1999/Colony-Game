// --------------------------- WAV Loader ---------------------------

#pragma pack(push, 1)
struct RiffHeader { uint32_t riff; uint32_t size; uint32_t wave; };
struct ChunkHeader { uint32_t id; uint32_t size; };
#pragma pack(pop)

bool XAudioEngine::LoadWav(const std::wstring& path, WavData& out, std::string* outErr) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (outErr) *outErr = "Failed to open WAV file";
        return false;
    }

    RiffHeader rh{};
    f.read(reinterpret_cast<char*>(&rh), sizeof(rh));
    const uint32_t RIFF = MakeTag('R','I','F','F');
    const uint32_t WAVE = MakeTag('W','A','V','E');
    if (!f || rh.riff != RIFF || rh.wave != WAVE) {
        if (outErr) *outErr = "Not a RIFF/WAVE file";
        return false;
    }

    bool haveFmt = false;
    bool haveData = false;
    std::vector<uint8_t> fmtBuf;
    std::vector<uint8_t> dataBuf;

    while (f && (!haveFmt || !haveData)) {
        ChunkHeader ch{};
        f.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        if (!f) break;

        if (ch.id == MakeTag('f','m','t',' ')) {
            fmtBuf.resize(ch.size);
            f.read(reinterpret_cast<char*>(fmtBuf.data()), ch.size);
            haveFmt = true;
        } else if (ch.id == MakeTag('d','a','t','a')) {
            dataBuf.resize(ch.size);
            f.read(reinterpret_cast<char*>(dataBuf.data()), ch.size);
            haveData = true;
        } else {
            // Skip unknown chunk
            f.seekg(ch.size, std::ios::cur);
        }

        // Chunks are word-aligned
        if (ch.size & 1) f.seekg(1, std::ios::cur);
    }

    if (!haveFmt || !haveData) {
        if (outErr) *outErr = "Missing fmt/data chunk";
        return false;
    }

    if (fmtBuf.size() < sizeof(WAVEFORMATEX)) {
        if (outErr) *outErr = "fmt chunk too small";
        return false;
    }

    // Parse format
    const WAVEFORMATEX* wfx = reinterpret_cast<const WAVEFORMATEX*>(fmtBuf.data());
    out.sampleBytesPerFrame = wfx->nBlockAlign;

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmtBuf.size() >= sizeof(WAVEFORMATEXTENSIBLE)) {
        out.isExtensible = true;
        std::memset(&out.fmtExt, 0, sizeof(out.fmtExt));
        std::memcpy(&out.fmtExt, fmtBuf.data(), sizeof(WAVEFORMATEXTENSIBLE));
    } else {
        out.isExtensible = false;
        std::memset(&out.fmtExt, 0, sizeof(out.fmtExt));
        std::memcpy(&out.fmtExt, fmtBuf.data(), sizeof(WAVEFORMATEX)); // copy header fields
        out.fmtExt.Samples.wValidBitsPerSample = wfx->wBitsPerSample;   // best-effort
    }

    out.samples = std::move(dataBuf);
    return true;
}

