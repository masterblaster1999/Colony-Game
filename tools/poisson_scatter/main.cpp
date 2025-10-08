// Minimal CLI to generate Poisson-disk points as CSV.
// Example:
//   poisson_scatter --width 512 --height 512 --r 8 --k 30 --seed 1234 --wrap 1 > points.csv

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdint>
#include <optional>

#include "pcg/PoissonDisk2D.hpp"

struct Args {
    float width  = 256.f;
    float height = 256.f;
    float r      = 8.f;
    int   k      = 30;
    uint32_t seed = 1337u;
    bool wrap = false;
    std::optional<std::string> outPath; // if empty => stdout
};

static void print_usage(const char* exe) {
    std::cerr <<
    "Usage: " << exe << " [--width W] [--height H] [--r R] [--k K] [--seed N] [--wrap 0|1] [--out file.csv]\n"
    "Generates Poisson-disk points (2D) over [0,W) x [0,H) and writes CSV 'x,y'.\n";
}

static bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need = [&](int n){ if (i + n >= argc) { print_usage(argv[0]); return false; } return true; };

        if (arg == "--width"  && need(1))  a.width  = std::strtof(argv[++i], nullptr);
        else if (arg == "--height" && need(1)) a.height = std::strtof(argv[++i], nullptr);
        else if (arg == "--r" && need(1)) a.r = std::strtof(argv[++i], nullptr);
        else if (arg == "--k" && need(1)) a.k = std::atoi(argv[++i]);
        else if (arg == "--seed" && need(1)) a.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (arg == "--wrap" && need(1)) a.wrap = (std::atoi(argv[++i]) != 0);
        else if (arg == "--out" && need(1)) a.outPath = std::string(argv[++i]);
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); std::exit(0); }
        else { std::cerr << "Unknown arg: " << arg << "\n"; print_usage(argv[0]); return false; }
    }
    return true;
}

int main(int argc, char** argv)
{
    Args a;
    if (!parse_args(argc, argv, a)) return 1;

    pcg::PoissonParams2D P;
    P.width = a.width;
    P.height = a.height;
    P.r = a.r;
    P.k = a.k;
    P.seed = a.seed;
    P.wrap = a.wrap;
    // You can plug a mask here; by default all positions are allowed.
    P.allow = {};

    auto pts = pcg::poisson_disk_2d(P);

    // Output CSV
    std::ostream* out = &std::cout;
    std::ofstream file;
    if (a.outPath) {
        file.open(a.outPath->c_str(), std::ios::out | std::ios::trunc);
        if (!file) {
            std::cerr << "Failed to open output file: " << *a.outPath << "\n";
            return 2;
        }
        out = &file;
    }

    *out << "x,y\n";
    for (const auto& p : pts) {
        *out << p.x << "," << p.y << "\n";
    }

    return 0;
}
