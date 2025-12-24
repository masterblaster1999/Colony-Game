// ------------------------------ GOAP-lite Actions ------------------------------
// Each action can generate a sequence of concrete Jobs for the agent to execute.
// Preconditions are checked against agent + world. Effects mutate an internal "needs" projection.

struct World; // fwd

struct WorldState {
    int hunger, rest, morale;
    bool hasMeal = false;
};

struct GoapAction {
    std::string name;
    int cost = 1;
    std::function<bool(const Agent&, const World& , const WorldState&)> pre;
    std::function<void(WorldState&)> eff;
    std::function<std::vector<Job>(Agent&, World&)> makeJobs;
};

class ActionLibrary {
public:
    void add(GoapAction a){ lib_.push_back(std::move(a)); }
    const std::vector<GoapAction>& all() const { return lib_; }
private:
    std::vector<GoapAction> lib_;
};

