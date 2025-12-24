// ------------------------------ Grid & Tiles ------------------------------

struct Tile {
    bool walkable = true;
    bool reserved = false;
    uint8_t material = 0;   // 0 soil, 1 tree, 2 rock, 3 water, 4 crop
    uint8_t terrain = 0;    // user-defined terrain kind (0 default)
    bool isDoor = false;
    bool doorOpen = false;
    uint16_t zoneId = 0;    // stockpile or room id
    uint16_t moveCost = 10; // base move cost (>=10)
};

class Grid {
public:
    Grid(int w, int h) : w_(w), h_(h), tiles_(w*h) {}
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }

    bool inBounds(const Vec2i& p) const noexcept {
        return (p.x>=0 && p.y>=0 && p.x<w_ && p.y<h_);
    }
    Tile& at(const Vec2i& p){ return tiles_[idx(p)]; }
    const Tile& at(const Vec2i& p) const { return tiles_[idx(p)]; }

    bool walkable(const Vec2i& p) const {
        if(!inBounds(p)) return false;
        const auto& t = at(p);
        return t.walkable && !t.reserved && (!t.isDoor || t.doorOpen); // closed door treated as blocked for path; open on approach
    }
    bool occupiable(const Vec2i& p) const {
        if(!inBounds(p)) return false;
        const auto& t = at(p);
        return t.walkable && !t.reserved; // doors considered occupiable (agent can open on arrival)
    }

    int moveCost(const Vec2i& p) const {
        if(!inBounds(p)) return 1000000;
        const auto& t = at(p);
        int c = t.moveCost;
        if(t.terrain==3) c += 15;           // e.g., shallow water penalty
        if(t.material==4) c += 5;           // crops slow a bit
        if(t.isDoor && !t.doorOpen) c += 25; // opening door cost
        return c;
    }

    std::array<Vec2i,8> neighbors8(const Vec2i& p) const {
        return {Vec2i{p.x+1,p.y}, {p.x-1,p.y}, {p.x,p.y+1}, {p.x,p.y-1},
                {p.x+1,p.y+1},{p.x+1,p.y-1},{p.x-1,p.y+1},{p.x-1,p.y-1}};
    }
    std::array<Vec2i,4> neighbors4(const Vec2i& p) const {
        return {Vec2i{p.x+1,p.y}, {p.x-1,p.y}, {p.x,p.y+1}, {p.x,p.y-1}};
    }

    void setObstacle(const Vec2i& p, bool blocked=true){
        if(!inBounds(p)) return; tiles_[idx(p)].walkable = !blocked; bumpStamp();
    }
    void setMaterial(const Vec2i& p, uint8_t m){
        if(!inBounds(p)) return; tiles_[idx(p)].material = m; bumpStamp();
    }
    void setTerrainCost(const Vec2i& p, uint16_t c){
        if(!inBounds(p)) return; tiles_[idx(p)].moveCost = std::max<uint16_t>(10, c); bumpStamp();
    }
    void setZoneId(const Vec2i& p, uint16_t id){
        if(!inBounds(p)) return; tiles_[idx(p)].zoneId = id; bumpStamp();
    }
    void setDoor(const Vec2i& p, bool isDoor, bool open=false){
        if(!inBounds(p)) return; auto& t=tiles_[idx(p)]; t.isDoor=isDoor; t.doorOpen=open; bumpStamp();
    }
    void openDoor(const Vec2i& p){ if(!inBounds(p)) return; tiles_[idx(p)].doorOpen=true; bumpStamp(); }
    void closeDoor(const Vec2i& p){ if(!inBounds(p)) return; tiles_[idx(p)].doorOpen=false; bumpStamp(); }

    void reserve(const Vec2i& p){ if(inBounds(p)){ tiles_[idx(p)].reserved = true; bumpStamp(); } }
    void unreserve(const Vec2i& p){ if(inBounds(p)){ tiles_[idx(p)].reserved = false; bumpStamp(); } }

    uint64_t stamp() const { return stamp_; }

private:
    size_t idx(const Vec2i& p) const { return static_cast<size_t>(p.y*w_ + p.x); }
    void bumpStamp(){ ++stamp_; }
    int w_{}, h_{};
    std::vector<Tile> tiles_;
    uint64_t stamp_ = 1; // increments on structural change
};

