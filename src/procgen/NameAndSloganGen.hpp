// src/procgen/NameAndSloganGen.hpp
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <array>
#include <random>
#include <algorithm>

namespace colony::procgen {

struct NameParams {
    int minLen = 4, maxLen = 10;
    uint64_t seed = 12345;
};

class Markov2 {
    // order-2 on characters; start token '^', end '$'
    std::unordered_map<std::string, std::vector<char>> T;
    std::mt19937_64 rng;
public:
    explicit Markov2(uint64_t seed=1) : rng(seed) {}
    void train(const std::vector<std::string>& corpus){
        auto add=[&](char a,char b,char c){ T[{a,b}].push_back(c); };
        for(const auto& w0 : corpus){
            std::string w="^"+w0+"$";
            if(w.size()<3) continue;
            for(size_t i=0;i+2<w.size();++i) add(w[i], w[i+1], w[i+2]);
        }
    }
    char next(char a,char b){
        auto it = T.find(std::string{a,b});
        if (it==T.end() || it->second.empty()) return '$';
        auto& v = it->second;
        std::uniform_int_distribution<size_t> ui(0, v.size()-1);
        return v[ui(rng)];
    }
    std::string generate(const NameParams& P){
        std::uniform_int_distribution<int> target(P.minLen, P.maxLen);
        int goal = target(rng);
        std::string out; out.reserve(goal+4);
        char a='^', b='^';
        while((int)out.size()<goal){
            char c = next(a,b);
            if (c=='$') { if((int)out.size()>=P.minLen) break; else { a='^'; b='^'; continue; } }
            out.push_back(c);
            a=b; b=c;
        }
        // capitalize
        if(!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
        return out;
    }
};

// Small syllable seed lists—extend with your own lore.
static inline const std::vector<std::string> kSettlementSeed = {
    "ash", "ford", "vale", "brook", "holm", "shire", "wood", "haven",
    "north", "east", "west", "south", "ridge", "pine", "ember", "mead", "stone", "hollow"
};
static inline const std::vector<std::string> kColonistSeed = {
    "ari", "ben", "cara", "dax", "elin", "finn", "gale", "hana",
    "ivan", "juno", "kael", "lina", "mara", "niko", "orin", "pax", "quin", "rya",
    "soren", "tala", "uly", "vida", "wyatt", "xeni", "yara", "zane"
};
static inline const std::vector<std::string> kFactionSeed = {
    "iron", "silver", "sun", "moon", "star", "dawn", "dusk", "cinder", "wolf", "spire", "azure", "crimson", "gild", "granite"
};

static inline std::vector<std::string> expand_corpus(const std::vector<std::string>& seed){
    std::vector<std::string> C; C.reserve(seed.size()*3);
    for (auto& s: seed){
        C.push_back(s);
        for (auto& t: seed){ if (&t==&s) continue; C.push_back(s+t); }
    }
    return C;
}

static inline std::string GenerateSettlementName(uint64_t seed, const NameParams& P=NameParams{}){
    Markov2 M(seed ^ 0xA55A);
    auto C = expand_corpus(kSettlementSeed);
    M.train(C);
    return M.generate(P);
}
static inline std::string GenerateColonistName(uint64_t seed, const NameParams& P=NameParams{}){
    Markov2 M(seed ^ 0xBEEF);
    auto C = expand_corpus(kColonistSeed);
    M.train(C);
    return M.generate(P);
}
static inline std::string GenerateFactionName(uint64_t seed, const NameParams& P=NameParams{}){
    Markov2 M(seed ^ 0xFEED);
    auto C = expand_corpus(kFactionSeed);
    M.train(C);
    return M.generate(P);
}

static inline std::string GenerateSlogan(uint64_t seed) {
    std::mt19937_64 rng(seed ^ 0x77);
    static const std::vector<std::string> Adj = {"Stalwart","Prosperous","Bold","Hidden","Verdant","Indomitable","Harmonious","Free","Honest","Radiant","Enduring"};
    static const std::vector<std::string> Noun= {"Frontier","Hearth","Commonwealth","Compact","Accord","Coalition","League","Pact","Sanctuary","Outpost","Union"};
    static const std::vector<std::string> Place={"North","East","West","South","Highlands","Lowlands","Valley","Ridge","Coast","Steppe"};
    static const std::vector<std::string> Motto={
        "From {place}, {adj} {noun}",
        "{adj} Hands, {adj2} Hearts",
        "By Soil and Star",
        "Work. Wisdom. {noun}.",
        "Many Voices, One {noun}",
        "In Storm and Sun, We Rise"
    };
    auto pick=[&](const auto& V){ std::uniform_int_distribution<size_t> ui(0,V.size()-1); return V[ui(rng)]; };
    std::string m = pick(Motto);
    auto repl=[&](const std::string& key, const std::string& val){
        size_t p=0; while((p=m.find(key,p))!=std::string::npos){ m.replace(p,key.size(),val); p+=val.size(); }
    };
    repl("{adj}", pick(Adj));
    repl("{adj2}", pick(Adj));
    repl("{noun}", pick(Noun));
    repl("{place}", pick(Place));
    return m;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Examples:
// auto name = GenerateSettlementName(123);
// auto citizen = GenerateColonistName(456);
// auto motto = GenerateSlogan(789);
#endif
