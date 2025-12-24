// ------------------------------ Workstations & Recipes ------------------------------

enum class BuildingType : uint8_t { None=0, Sawmill, Kitchen, ResearchBench, Forge };

struct Recipe {
    std::string name;
    std::vector<ItemStack> inputs;
    std::vector<ItemStack> outputs;
    int workTicks = 150;
    JobKind jobKind = JobKind::Craft;
};

struct Workstation {
    BuildingType type = BuildingType::None;
    Vec2i pos{};
    std::vector<Recipe> recipes;
    bool busy=false;
    // local buffers (very lightweight)
    std::vector<ItemStack> inbuf, outbuf;
};

class BuildingManager {
public:
    int add(BuildingType t, Vec2i p){
        Workstation w; w.type=t; w.pos=p;
        switch(t){
            case BuildingType::Sawmill:
                w.recipes.push_back({"Planks", {{ItemId::Log,1}}, {{ItemId::Plank,1}}, 120, JobKind::Craft});
                break;
            case BuildingType::Kitchen:
                w.recipes.push_back({"CookMeal", {{ItemId::RawFood,1}}, {{ItemId::Meal,1}}, 140, JobKind::Cook});
                break;
            case BuildingType::ResearchBench:
                w.recipes.push_back({"Research", {{ItemId::Paper,1}}, {{ItemId::ResearchData,1}}, 200, JobKind::Research});
                break;
            case BuildingType::Forge:
                w.recipes.push_back({"Smelt", {{ItemId::Ore,1}}, {{ItemId::Ingot,1}}, 180, JobKind::Craft});
                break;
            default: break;
        }
        ws_.push_back(std::move(w));
        return (int)ws_.size()-1;
    }
    std::vector<Workstation>& all(){ return ws_; }
    const std::vector<Workstation>& all() const { return ws_; }
    Workstation* nearest(BuildingType t, const Vec2i& from){
        Workstation* best=nullptr; int bd=INT_MAX;
        for(auto& w: ws_) if(w.type==t){
            int d=from.manhattan(w.pos); if(d<bd){ bd=d; best=&w; }
        }
        return best;
    }
private:
    std::vector<Workstation> ws_;
};

