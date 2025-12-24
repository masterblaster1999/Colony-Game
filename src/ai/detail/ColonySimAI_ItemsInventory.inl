// ------------------------------ Items & Inventory ------------------------------

enum class ItemId : uint16_t {
    None=0, Log, Plank, Ore, Ingot, RawFood, Meal, Herb, Medicine, Paper, ResearchData, Tool, Seed, Crop, Stone
};

static inline const char* itemName(ItemId id){
    switch(id){
        case ItemId::Log: return "Log"; case ItemId::Plank: return "Plank";
        case ItemId::Ore: return "Ore"; case ItemId::Ingot: return "Ingot";
        case ItemId::RawFood: return "RawFood"; case ItemId::Meal: return "Meal";
        case ItemId::Herb: return "Herb"; case ItemId::Medicine: return "Medicine";
        case ItemId::Paper: return "Paper"; case ItemId::ResearchData: return "ResearchData";
        case ItemId::Tool: return "Tool"; case ItemId::Seed: return "Seed";
        case ItemId::Crop: return "Crop"; case ItemId::Stone: return "Stone";
        default: return "None";
    }
}

struct ItemStack {
    ItemId id = ItemId::None;
    int qty = 0;
    bool empty() const { return id==ItemId::None || qty<=0; }
};

class Inventory {
public:
    explicit Inventory(int cap=16) : cap_(cap) {}
    int capacity() const { return cap_; }
    int count(ItemId id) const {
        int n=0; for(auto& s: slots_) if(s.id==id) n+=s.qty; return n;
    }
    int total() const { int n=0; for(auto&s:slots_) n+=s.qty; return n; }
    bool has(ItemId id, int qty) const { return count(id) >= qty; }

    // add up to qty; returns leftover
    int add(ItemId id, int qty){
        if(id==ItemId::None || qty<=0) return 0;
        // try stack
        for(auto& s: slots_) if(s.id==id && s.qty>0){ s.qty += qty; return 0; }
        // add new slot
        if((int)slots_.size()<cap_){ slots_.push_back({id, qty}); return 0; }
        return qty; // no space, all leftover
    }
    // remove up to qty; returns removed
    int remove(ItemId id, int qty){
        int need=qty, got=0;
        for(auto& s: slots_){
            if(s.id!=id || s.qty<=0) continue;
            int take = std::min(s.qty, need);
            s.qty -= take; got += take; need -= take;
            if(s.qty<=0){ s.id=ItemId::None; s.qty=0; }
            if(need<=0) break;
        }
        // cleanup empties
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const ItemStack& s){ return s.empty(); }), slots_.end());
        return got;
    }
    const std::vector<ItemStack>& slots() const { return slots_; }
    std::vector<ItemStack>& slots() { return slots_; }
private:
    int cap_;
    std::vector<ItemStack> slots_;
};

