// ============ Grid ============

class Grid {
public:
    Grid() { reset(1,1); }
    Grid(int w, int h) { reset(w, h); }

    void reset(int w, int h) {
        _w = std::max(1, w); _h = std::max(1, h);
        _blocked.assign((size_t)_w * _h, uint8_t{0});
        _cost.assign((size_t)_w * _h, uint16_t{1});
        _rev = 1;
        _everNonUnitCost = false;
    }

    int  width()  const noexcept { return _w; }
    int  height() const noexcept { return _h; }

    bool inBounds(int x, int y) const noexcept {
        return (unsigned)x < (unsigned)_w && (unsigned)y < (unsigned)_h;
    }

    bool isBlocked(int x, int y) const noexcept {
        assert(inBounds(x,y));
        return _blocked[idx(x,y)] != 0;
    }

    void setBlocked(int x, int y, bool blocked) noexcept {
        assert(inBounds(x,y));
        uint8_t& b = _blocked[idx(x,y)];
        uint8_t nb = blocked ? 1u : 0u;
        if (b != nb) { b = nb; ++_rev; }
    }

    uint16_t moveCost(int x, int y) const noexcept {
        assert(inBounds(x,y));
        return _cost[idx(x,y)];
    }

    void setMoveCost(int x, int y, uint16_t c) noexcept {
        assert(inBounds(x,y));
        uint16_t nc = std::max<uint16_t>(1, c);
        uint16_t& ref = _cost[idx(x,y)];
        if (ref != nc) {
            ref = nc; ++_rev;
            if (nc != 1) _everNonUnitCost = true;
        }
    }

    bool uniformCostEver() const noexcept { return !_everNonUnitCost; } // conservative

    size_t idx(int x, int y) const noexcept { return (size_t)y * _w + x; }
    int    xof(size_t i) const noexcept { return (int)(i % _w); }
    int    yof(size_t i) const noexcept { return (int)(i / _w); }

    uint64_t revision() const noexcept { return _rev; }

    // --- Serialization ---
    void serialize(std::ostream& os) const {
        os.write((const char*)&_w, sizeof(_w));
        os.write((const char*)&_h, sizeof(_h));
        os.write((const char*)&_rev, sizeof(_rev));
        os.write((const char*)&_everNonUnitCost, sizeof(_everNonUnitCost));
        os.write((const char*)_blocked.data(), _blocked.size()*sizeof(uint8_t));
        os.write((const char*)_cost.data(), _cost.size()*sizeof(uint16_t));
    }
    bool deserialize(std::istream& is) {
        int w=0,h=0; uint64_t rv=0; bool enu=false;
        if (!is.read((char*)&w, sizeof(w))) return false;
        if (!is.read((char*)&h, sizeof(h))) return false;
        if (!is.read((char*)&rv, sizeof(rv))) return false;
        if (!is.read((char*)&enu, sizeof(enu))) return false;
        reset(w,h);
        _rev = rv;
        _everNonUnitCost = enu;
        if (!is.read((char*)_blocked.data(), _blocked.size()*sizeof(uint8_t))) return false;
        if (!is.read((char*)_cost.data(), _cost.size()*sizeof(uint16_t))) return false;
        return true;
    }

private:
    int _w = 1, _h = 1;
    std::vector<uint8_t>  _blocked;
    std::vector<uint16_t> _cost;
    uint64_t _rev = 0;
    bool _everNonUnitCost = false;
};

