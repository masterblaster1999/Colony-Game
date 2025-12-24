// ------------------------------ Utilities ------------------------------
struct Vec2i {
    int x = 0, y = 0;
    constexpr Vec2i() = default;
    constexpr Vec2i(int x_, int y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Vec2i& o) const noexcept { return x==o.x && y==o.y; }
    constexpr bool operator!=(const Vec2i& o) const noexcept { return !(*this==o); }
    constexpr bool operator<(const Vec2i& o) const noexcept { return (y<o.y)||((y==o.y)&&(x<o.x)); }
    constexpr Vec2i operator+(const Vec2i& o) const noexcept { return {x+o.x, y+o.y}; }
    constexpr Vec2i operator-(const Vec2i& o) const noexcept { return {x-o.x, y-o.y}; }
    constexpr int manhattan(const Vec2i& o) const noexcept { return std::abs(x-o.x)+std::abs(y-o.y); }
    constexpr int chebyshev(const Vec2i& o) const noexcept { return std::max(std::abs(x-o.x), std::abs(y-o.y)); }
};

struct Hasher {
    size_t operator()(const Vec2i& v) const noexcept {
        uint64_t a = (static_cast<uint64_t>(static_cast<uint32_t>(v.x)) << 32) ^ static_cast<uint32_t>(v.y);
        a ^= (a >> 33); a *= 0xff51afd7ed558ccdULL; a ^= (a >> 33);
        a *= 0xc4ceb9fe1a85ec53ULL; a ^= (a >> 33);
        return static_cast<size_t>(a);
    }
};

template <class T, class U>
struct PairHasher {
    size_t operator()(const std::pair<T,U>& p) const noexcept {
        std::hash<T> ht; std::hash<U> hu;
        uint64_t a = static_cast<uint64_t>(ht(p.first));
        uint64_t b = static_cast<uint64_t>(hu(p.second));
        a ^= (b + 0x9e3779b97f4a7c15ULL + (a<<6) + (a>>2));
        return static_cast<size_t>(a);
    }
};

class StopWatch {
public:
    using clock = std::chrono::high_resolution_clock;
    StopWatch() : start_(clock::now()) {}
    void reset() { start_ = clock::now(); }
    double seconds() const {
        return std::chrono::duration<double>(clock::now()-start_).count();
    }
private:
    clock::time_point start_;
};

class RNG {
public:
    explicit RNG(uint64_t seed = 0xC01oNYULL) : eng_(seed ? seed : std::random_device{}()) {}
    int uniformInt(int a, int b){ std::uniform_int_distribution<int> d(a,b); return d(eng_);}
    double uniform01(){ std::uniform_real_distribution<double> d(0,1); return d(eng_);}
    template<typename It>
    auto pick(It begin, It end){
        auto n = std::distance(begin,end);
        if(n<=0) return begin;
        std::uniform_int_distribution<long long> d(0, n-1);
        std::advance(begin, d(eng_));
        return begin;
    }
    uint64_t seed() const { return seed_; }
private:
    uint64_t seed_ = 0xC01oNYULL;
    std::mt19937_64 eng_;
};

static inline std::string join(const std::vector<std::string>& v, char sep=','){
    std::ostringstream o;
    for(size_t i=0;i<v.size();++i){ if(i) o<<sep; o<<v[i]; }
    return o.str();
}
static inline std::vector<std::string> split(const std::string& s, char sep=' '){
    std::vector<std::string> out; std::string cur;
    for(char c: s){ if(c==sep){ if(!cur.empty()) out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if(!cur.empty()) out.push_back(cur);
    return out;
}

