#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>

namespace colony::terrain {

class Heightfield {
public:
    Heightfield() = default;
    Heightfield(int w, int h, float init=0.0f) { resize(w,h,init); }

    void   resize(int w, int h, float init=0.0f) {
        m_w=w; m_h=h; m_hm.assign(size_t(w)*h, init);
    }
    int    width()  const { return m_w; }
    int    height() const { return m_h; }
    size_t size()   const { return m_hm.size(); }

    float& at(int x,int y)       { return m_hm[size_t(y)*m_w + x]; }
    float  at(int x,int y) const { return m_hm[size_t(y)*m_w + x]; }
    const float* data() const { return m_hm.data(); }
    float*       data()       { return m_hm.data(); }

    void clamp(float lo, float hi) {
        for (auto& v : m_hm) v = std::clamp(v, lo, hi);
    }

private:
    int m_w=0, m_h=0;
    std::vector<float> m_hm;
};

} // namespace colony::terrain
