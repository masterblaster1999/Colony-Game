#include "Wav.h"
#include <cstring>

using namespace wav;

static void wfx_deleter(void* p){ delete[] reinterpret_cast<uint8_t*>(p); }

static uint32_t ReadU32(FILE* f){
    uint32_t v; if(fread(&v,1,4,f)!=4) throw std::runtime_error("Unexpected EOF");
    return v;
}

static uint16_t ReadU16(FILE* f){
    uint16_t v; if(fread(&v,1,2,f)!=2) throw std::runtime_error("Unexpected EOF");
    return v;
}

WavInfo wav::ReadHeader(const std::wstring& path)
{
    WavInfo info{};
    FILE* f=nullptr;
    if(_wfopen_s(&f, path.c_str(), L"rb") || !f) throw std::runtime_error("Open failed");

    auto closeF = std::unique_ptr<FILE, int(*)(FILE*)>(f, fclose);

    // RIFF header
    char riff[4]; if(fread(riff,1,4,f)!=4 || memcmp(riff,"RIFF",4)!=0) throw std::runtime_error("Not RIFF");
    (void)ReadU32(f); // file size
    char wave[4]; if(fread(wave,1,4,f)!=4 || memcmp(wave,"WAVE",4)!=0) throw std::runtime_error("Not WAVE");

    // Find 'fmt ' chunk
    bool haveFmt=false, haveData=false;
    std::unique_ptr<uint8_t[]> fmtbuf;
    uint32_t fmtSize=0;

    while(!haveFmt || !haveData){
        char id[4]; if(fread(id,1,4,f)!=4) throw std::runtime_error("Unexpected EOF (chunk id)");
        uint32_t size = ReadU32(f);
        if(!memcmp(id,"fmt ",4)){
            fmtbuf = std::unique_ptr<uint8_t[]>(new uint8_t[size]);
            if(fread(fmtbuf.get(),1,size,f)!=size) throw std::runtime_error("EOF (fmt)");
            fmtSize = size;
            haveFmt = true;
        } else if(!memcmp(id,"data",4)){
            info.dataOffset = _ftelli64(f);
            info.dataBytes  = size;
            _fseeki64(f, size, SEEK_CUR);
            haveData = true;
        } else {
            _fseeki64(f, size, SEEK_CUR); // skip
        }
    }

    if(!haveFmt || !haveData) throw std::runtime_error("Missing fmt or data");

    // Normalize to WAVEFORMATEX / EXTENSIBLE
    WAVEFORMATEX* wfx = nullptr;
    if(fmtSize >= sizeof(WAVEFORMATEX)){
        auto tag = reinterpret_cast<WAVEFORMATEX*>(fmtbuf.get())->wFormatTag;
        if(tag == WAVE_FORMAT_PCM || tag == WAVE_FORMAT_IEEE_FLOAT){
            // Copy exact fmt payload
            wfx = reinterpret_cast<WAVEFORMATEX*>(new uint8_t[fmtSize]);
            memcpy(wfx, fmtbuf.get(), fmtSize);
        } else if(tag == WAVE_FORMAT_EXTENSIBLE && fmtSize >= sizeof(WAVEFORMATEXTENSIBLE)){
            wfx = reinterpret_cast<WAVEFORMATEX*>(new uint8_t[sizeof(WAVEFORMATEXTENSIBLE)]);
            memcpy(wfx, fmtbuf.get(), sizeof(WAVEFORMATEXTENSIBLE));
        } else {
            throw std::runtime_error("Unsupported WAV format tag");
        }
    } else throw std::runtime_error("Bad fmt chunk");

    info.wfx = wfx;
    return info;
}

std::pair<std::unique_ptr<WAVEFORMATEX[], void(*)(void*)>, std::vector<uint8_t>>
wav::LoadWholeFile(const std::wstring& path)
{
    auto info = ReadHeader(path);
    auto fmt  = std::unique_ptr<WAVEFORMATEX[], void(*)(void*)>(info.wfx, wfx_deleter);

    FILE* f=nullptr; if(_wfopen_s(&f, path.c_str(), L"rb") || !f) throw std::runtime_error("Open failed");
    auto closeF = std::unique_ptr<FILE, int(*)(FILE*)>(f, fclose);
    _fseeki64(f, info.dataOffset, SEEK_SET);

    std::vector<uint8_t> bytes; bytes.resize(size_t(info.dataBytes));
    if(info.dataBytes && fread(bytes.data(),1,size_t(info.dataBytes),f) != size_t(info.dataBytes))
        throw std::runtime_error("EOF reading data");

    return { std::move(fmt), std::move(bytes) };
}
