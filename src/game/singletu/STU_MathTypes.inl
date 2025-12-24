// ============================== Math & Types =================================

struct Vec2i { int x=0, y=0; bool operator==(const Vec2i& o) const { return x==o.x && y==o.y; } };
static inline Vec2i operator+(Vec2i a, Vec2i b){ return {a.x+b.x,a.y+b.y}; }
static inline Vec2i operator-(Vec2i a, Vec2i b){ return {a.x-b.x,a.y-b.y}; }
namespace std {
template<> struct hash<Vec2i> {
    size_t operator()(const Vec2i& v) const noexcept {
        return (uint64_t(uint32_t(v.x))<<32) ^ uint32_t(v.y);
    }
};
}

