#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include "procgen/WorldGen.h"
#include "procgen/Types.h"

using namespace procgen;

static void save_ppm(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, (size_t)w*h*3, f);
    std::fclose(f);
}

static std::vector<uint8_t> height_to_rgb(const std::vector<float>& h, int w, int hgt) {
    std::vector<uint8_t> rgb((size_t)w*hgt*3);
    for (int i=0;i<w*hgt;++i) {
        uint8_t v = (uint8_t)std::max(0.f, std::min(255.f, h[i]*255.f));
        rgb[i*3+0]=v; rgb[i*3+1]=v; rgb[i*3+2]=v;
    }
    return rgb;
}

static std::vector<uint8_t> field_to_rgb(const std::vector<float>& f, int w, int hgt, int channel) {
    std::vector<uint8_t> rgb((size_t)w*hgt*3, 0);
    for (int i=0;i<w*hgt;++i) {
        uint8_t v = (uint8_t)std::max(0.f, std::min(255.f, f[i]*255.f));
        rgb[i*3 + channel] = v;
    }
    return rgb;
}

int main(int argc, char** argv) {
    WorldParams p;
    if (argc >= 2) p.seed   = (uint32_t)std::strtoul(argv[1], nullptr, 10);
    if (argc >= 3) p.width  = std::atoi(argv[2]);
    if (argc >= 4) p.height = std::atoi(argv[3]);

    auto w = generateWorld(p);

    auto h_rgb = height_to_rgb(w.height, w.w, w.h);
    save_ppm("height.ppm", w.w, w.h, h_rgb);

    auto m_rgb = field_to_rgb(w.moisture, w.w, w.h, 1); // G channel
    save_ppm("moisture.ppm", w.w, w.h, m_rgb);

    auto t_rgb = field_to_rgb(w.temperature, w.w, w.h, 0); // R channel
    save_ppm("temperature.ppm", w.w, w.h, t_rgb);

    auto biome_rgba = makeBiomePreviewRGBA(w);
    // strip alpha for PPM
    std::vector<uint8_t> biome_rgb((size_t)w.w*w.h*3);
    for (int i=0;i<w.w*w.h;++i) {
        biome_rgb[i*3+0] = biome_rgba[i*4+0];
        biome_rgb[i*3+1] = biome_rgba[i*4+1];
        biome_rgb[i*3+2] = biome_rgba[i*4+2];
    }
    save_ppm("biome.ppm", w.w, w.h, biome_rgb);

    // dump resources as CSV
    std::ofstream ofs("resources.csv");
    ofs << "type,x,y\n";
    for (auto& r : w.resources) {
        ofs << (int)r.type << "," << r.x << "," << r.y << "\n";
    }

    std::printf("Generated world %dx%d seed=%u\n", w.w, w.h, p.seed);
    std::printf("Outputs: height.ppm, moisture.ppm, temperature.ppm, biome.ppm, resources.csv\n");
    return 0;
}
