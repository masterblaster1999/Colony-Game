//======================================================================================
// High-resolution timing
//======================================================================================
class Timer {
public:
    Timer() { QueryPerformanceFrequency(&freq_); Reset(); }
    void Reset() { QueryPerformanceCounter(&last_); acc_=0.0; }
    double Tick() {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last_.QuadPart)/double(freq_.QuadPart);
        last_ = now;
        acc_ += dt;
        return dt;
    }
    double Accum() const { return acc_; }
    void ClearAcc() { acc_ = 0.0; }
private:
    LARGE_INTEGER freq_{}, last_{};
    double acc_ = 0.0;
};

//======================================================================================
// Simple RNG
//======================================================================================
class Rng {
public:
    explicit Rng(uint64_t seed) : eng_(seed) {}
    int irange(int lo, int hi) { std::uniform_int_distribution<int> d(lo,hi); return d(eng_); }
    bool chance(double p)      { std::bernoulli_distribution d(p); return d(eng_); }
    double frand(double a=0.0, double b=1.0) { std::uniform_real_distribution<double> d(a,b); return d(eng_); }
private:
    std::mt19937_64 eng_;
};

