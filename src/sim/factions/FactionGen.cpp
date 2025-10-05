#include "FactionGen.hpp"
#include <random>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>

namespace colony
{
    // ------------------- helpers -------------------

    static inline uint64_t fnv1a64(uint64_t h, const uint8_t* data, size_t len)
    {
        constexpr uint64_t prime = 1099511628211ull;
        for (size_t i = 0; i < len; ++i)
        {
            h ^= data[i];
            h *= prime;
        }
        return h;
    }

    uint64_t FactionGenerator::sub_seed(uint64_t world_seed, std::string_view tag)
    {
        constexpr uint64_t basis = 1469598103934665603ull; // FNV offset
        uint64_t h = basis ^ world_seed;
        h = fnv1a64(h, reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
        return h;
    }

    // Uniform [0,1)
    static inline float urand01(std::mt19937_64& rng)
    {
        return std::generate_canonical<float, 24>(rng);
    }

    static inline int irand(std::mt19937_64& rng, int lo, int hi_inclusive)
    {
        std::uniform_int_distribution<int> d(lo, hi_inclusive);
        return d(rng);
    }

    template <class T>
    static const T& weighted_pick(std::mt19937_64& rng, const std::vector<T>& items,
                                  const std::function<float(const T&)>& weight_of)
    {
        float total = 0.f;
        for (auto& it : items) total += std::max(0.f, weight_of(it));
        if (total <= 0.f) return items.front();

        std::uniform_real_distribution<float> d(0.f, total);
        float r = d(rng);
        float acc = 0.f;
        for (auto& it : items)
        {
            acc += std::max(0.f, weight_of(it));
            if (r <= acc) return it;
        }
        return items.back();
    }

    // quick clamp for C++17
    template <class T>
    static inline T clamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }

    // HSV->RGB for color variety when no palette is provided
    static Color hsv_to_rgb(float H, float S, float V)
    {
        H = std::fmodf(H, 360.f); if (H < 0) H += 360.f;
        float C = V * S;
        float X = C * (1 - std::fabsf(std::fmodf(H / 60.f, 2.f) - 1.f));
        float m = V - C;
        float r=0,g=0,b=0;
        if (      H < 60)  { r=C; g=X; b=0; }
        else if ( H < 120) { r=X; g=C; b=0; }
        else if ( H < 180) { r=0; g=C; b=X; }
        else if ( H < 240) { r=0; g=X; b=C; }
        else if ( H < 300) { r=X; g=0; b=C; }
        else               { r=C; g=0; b=X; }
        Color out;
        out.r = static_cast<uint8_t>(clamp((r+m)*255.f, 0.f, 255.f));
        out.g = static_cast<uint8_t>(clamp((g+m)*255.f, 0.f, 255.f));
        out.b = static_cast<uint8_t>(clamp((b+m)*255.f, 0.f, 255.f));
        return out;
    }

    // Syllable-based lightweight name generator
    static std::string make_name(std::mt19937_64& rng, Ethos e)
    {
        static const char* syll[] = {
            "al","an","ar","ash","bar","bel","dor","dra","el","fa","gor","ik",
            "ka","kor","la","mor","na","or","ra","rin","sha","sil","tor","ul","va","vor","zen"
        };
        static const char* titles_traders[] = {"Guild","Company","Syndicate","Consortium","Exchange"};
        static const char* titles_raiders[] = {"Band","Reavers","Marauders","Host","Riders"};
        static const char* titles_settlers[] = {"Colony","League","Union","Fellowship","Council"};
        static const char* titles_nomads[] = {"Caravan","Clan","Horde","Walkers","Drifters"};
        static const char* titles_scholars[] = {"Order","College","Archive","Conclave","Society"};
        static const char* titles_cultists[] = {"Cult","Circle","Cabal","Sect","Choir"};

        auto pick = [&](const char* const* arr, int n){ return std::string(arr[irand(rng,0,n-1)]); };

        std::string root;
        int parts = irand(rng, 2, 3);
        for (int i=0;i<parts;i++) root += syll[irand(rng,0,(int)(sizeof(syll)/sizeof(*syll))-1)];
        // Capitalize first
        if (!root.empty()) root[0] = (char)std::toupper(root[0]);

        std::string title;
        switch (e)
        {
            case Ethos::Traders:  title = pick(titles_traders,  (int)(sizeof(titles_traders)/sizeof(*titles_traders))); break;
            case Ethos::Raiders:  title = pick(titles_raiders,  (int)(sizeof(titles_raiders)/sizeof(*titles_raiders))); break;
            case Ethos::Settlers: title = pick(titles_settlers, (int)(sizeof(titles_settlers)/sizeof(*titles_settlers))); break;
            case Ethos::Nomads:   title = pick(titles_nomads,   (int)(sizeof(titles_nomads)/sizeof(*titles_nomads)));   break;
            case Ethos::Scholars: title = pick(titles_scholars, (int)(sizeof(titles_scholars)/sizeof(*titles_scholars))); break;
            case Ethos::Cultists: title = pick(titles_cultists, (int)(sizeof(titles_cultists)/sizeof(*titles_cultists))); break;
        }

        // e.g., "The Vorasha Guild"
        return "The " + root + " " + title;
    }

    float FactionGenerator::ethos_affinity(Ethos a, Ethos b)
    {
        if (a == b)
        {
            // Same-ethos bias toward positive relations
            switch (a)
            {
                case Ethos::Raiders:  return  0.1f;
                case Ethos::Cultists: return  0.0f;
                default:              return  0.4f;
            }
        }

        // Pair-specific nudges; compact and readable
        auto pair = [&](Ethos x, Ethos y){ return (a==x && b==y) || (a==y && b==x); };

        if (pair(Ethos::Traders,  Ethos::Settlers)) return +0.35f;
        if (pair(Ethos::Traders,  Ethos::Raiders))  return -0.65f;
        if (pair(Ethos::Raiders,  Ethos::Settlers)) return -0.45f;
        if (pair(Ethos::Raiders,  Ethos::Scholars)) return -0.35f;
        if (pair(Ethos::Nomads,   Ethos::Settlers)) return -0.10f;
        if (pair(Ethos::Scholars, Ethos::Cultists)) return -0.55f;
        if (pair(Ethos::Traders,  Ethos::Scholars)) return +0.20f;

        return 0.0f; // neutral otherwise
    }

    // ------------------- generate() -------------------

    FactionSet FactionGenerator::generate(const FactionGenParams& P)
    {
        FactionSet out;

        // rng
        std::mt19937_64 rng(sub_seed(P.world_seed, "factions"));

        // how many factions?
        int nmin = std::max(1, P.min_factions);
        int nmax = std::max(nmin, P.max_factions);
        int N = irand(rng, nmin, nmax);

        // local lambdas
        auto pick_archetype = [&]()->const FactionArchetype&
        {
            return weighted_pick(rng, P.archetypes, [](auto& a){ return a.weight; });
        };

        auto pick_color = [&]()->Color
        {
            if (!P.palette.empty())
            {
                auto& c = P.palette[irand(rng, 0, (int)P.palette.size()-1)];
                return Color{c[0], c[1], c[2]};
            }
            float h = urand01(rng) * 360.f;
            float s = 0.45f + 0.4f * urand01(rng);
            float v = 0.70f + 0.25f * urand01(rng);
            return hsv_to_rgb(h, s, v);
        };

        auto good_spot = [&](int x, int y) -> bool
        {
            if (P.is_blocked && P.is_blocked(x,y))
                return false;
            float s = 1.f;
            if (P.habitat_score) s = P.habitat_score(x, y);
            return urand01(rng) < s; // accept by habitat score
        };

        auto too_close = [&](int x, int y, const std::vector<Faction>& acc) -> bool
        {
            const int minD2 = P.min_base_spacing * P.min_base_spacing;
            for (auto& f : acc)
            {
                int dx = x - f.base.x;
                int dy = y - f.base.y;
                if (dx*dx + dy*dy < minD2) return true;
            }
            return false;
        };

        // build factions
        out.factions.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const FactionArchetype& A = pick_archetype();

            // scalar attributes
            auto urand = [&](float a, float b){ std::uniform_real_distribution<float> d(a,b); return d(rng); };

            Faction F;
            F.id          = static_cast<uint32_t>(i);
            F.ethos       = A.ethos;
            F.tech        = urand(A.tech_min, A.tech_max);
            F.aggression  = urand(A.aggression_min, A.aggression_max);
            F.hospitality = urand(A.hospitality_min, A.hospitality_max);
            F.color       = pick_color();

            // place base with rejection sampling
            const int max_tries = 400;
            int tries = 0;
            do
            {
                F.base.x = irand(rng, 0, P.map_width  - 1);
                F.base.y = irand(rng, 0, P.map_height - 1);
                ++tries;
            }
            while ((tries < max_tries) && (!good_spot(F.base.x, F.base.y) || too_close(F.base.x, F.base.y, out.factions)));

            // generate name at the end (after ethos decided)
            F.name = make_name(rng, F.ethos);

            out.factions.push_back(F);
        }

        // relations: symmetric, compact heuristic:
        // base = ethos compatibility
        // minus aggression pressure
        // plus small RNG nudge
        const int M = (int)out.factions.size();
        out.relations.assign(M*M, 0.0f);
        for (int i=0;i<M;i++)
        for (int j=i;j<M;j++)
        {
            float base = ethos_affinity(out.factions[i].ethos, out.factions[j].ethos);

            float aggr = 0.5f * (out.factions[i].aggression + out.factions[j].aggression);
            float hosp = 0.5f * (out.factions[i].hospitality + out.factions[j].hospitality);

            float jitter = (urand01(rng) - 0.5f) * 0.20f; // +/-0.1

            float r = base - 0.6f*aggr + 0.4f*hosp + jitter;
            r = clamp(r, -1.0f, 1.0f);

            out.relations[i*M+j] = out.relations[j*M+i] = (i==j ? 1.0f : r);
        }

        return out;
    }
}
