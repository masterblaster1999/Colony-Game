// ------------------------------ Stockpiles & Ground Items ------------------------------

struct StockpileZone {
    uint16_t id = 0;
    std::unordered_set<Vec2i, Hasher> cells;
    std::unordered_set<ItemId> allow;   // empty = allow all
    int priority = 0;                   // 0 normal, higher first
};

class Stockpiles {
public:
    uint16_t createZone(int priority=0){
        uint16_t id = ++nextId_;
        zones_.push_back({id,{}, {}, priority});
        return id;
    }
    void addCell(uint16_t id, const Vec2i& p){
        if(auto* z = find(id)) z->cells.insert(p);
    }
    void setAllow(uint16_t id, const std::vector<ItemId>& items){
        if(auto* z = find(id)){ z->allow.clear(); z->allow.insert(items.begin(), items.end()); }
    }
    std::optional<uint16_t> zoneIdAt(const Vec2i& p) const {
        for (auto& z: zones_) if(z.cells.count(p)) return z.id; return std::nullopt;
    }
    const std::vector<StockpileZone>& zones() const { return zones_; }

    // choose best cell for item; naive: any empty cell in highest-priority matching zone
    std::optional<Vec2i> pickDestination(ItemId item, const Vec2i& near) const {
        const StockpileZone* bestZ = nullptr;
        for (auto& z: zones_) if(z.allow.empty() || z.allow.count(item)){
            if(!bestZ || z.priority>bestZ->priority) bestZ=&z;
        }
        if(!bestZ) return std::nullopt;
        // pick by nearest to 'near'
        const Vec2i* bestCell=nullptr; int bestDist=INT_MAX;
        for (const auto& c: bestZ->cells){
            int d = c.manhattan(near);
            if(d<bestDist){ bestDist=d; bestCell=&c; }
        }
        if(bestCell) return *bestCell; return std::nullopt;
    }
private:
    StockpileZone* find(uint16_t id){
        for(auto& z: zones_) if(z.id==id) return &z; return nullptr;
    }
    uint16_t nextId_ = 0;
    std::vector<StockpileZone> zones_;
};

class GroundItems {
public:
    void drop(const Vec2i& at, ItemId id, int qty){
        if(qty<=0 || id==ItemId::None) return;
        auto& v = items_[at];
        // stack same id
        for(auto& s: v){ if(s.id==id){ s.qty+=qty; return; } }
        v.push_back({id, qty});
    }
    // take up to qty; returns removed
    int take(const Vec2i& at, ItemId id, int qty){
        if(qty<=0) return 0;
        auto it = items_.find(at);
        if(it==items_.end()) return 0;
        int need=qty, got=0;
        auto& v = it->second;
        for(auto& s: v){
            if(s.id!=id) continue;
            int take = std::min(s.qty, need);
            s.qty -= take; got += take; need -= take;
            if(need<=0) break;
        }
        v.erase(std::remove_if(v.begin(), v.end(), [](const ItemStack& s){ return s.empty(); }), v.end());
        if(v.empty()) items_.erase(at);
        return got;
    }
    const std::vector<ItemStack>* at(const Vec2i& p) const {
        auto it = items_.find(p);
        if(it==items_.end()) return nullptr; return &it->second;
    }
    std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher>& mut(){ return items_; }
    const std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher>& all() const { return items_; }
private:
    std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher> items_;
};

