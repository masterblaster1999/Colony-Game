// ------------------------------ Jobs ------------------------------

enum class JobKind : uint16_t {
    None=0,
    MoveTo, Chop, Mine, Haul, Build, Farm,
    Craft, Cook, Research, Heal, Train, Tame, Patrol, Trade, Deliver,
    JobKind_Count
};
static inline const char* jobName(JobKind k){
    switch(k){
        case JobKind::MoveTo: return "MoveTo"; case JobKind::Chop: return "Chop";
        case JobKind::Mine: return "Mine"; case JobKind::Haul: return "Haul";
        case JobKind::Build: return "Build"; case JobKind::Farm: return "Farm";
        case JobKind::Craft: return "Craft"; case JobKind::Cook: return "Cook";
        case JobKind::Research: return "Research"; case JobKind::Heal: return "Heal";
        case JobKind::Train: return "Train"; case JobKind::Tame: return "Tame";
        case JobKind::Patrol: return "Patrol"; case JobKind::Trade: return "Trade";
        case JobKind::Deliver: return "Deliver";
        default: return "None";
    }
}

struct Job {
    JobKind kind = JobKind::None;
    Vec2i target{};      // primary tile
    Vec2i aux{};         // secondary tile (e.g., haul destination)
    int   workTicks = 60;
    // Item payloads (for haul/craft/cook/trade)
    ItemId item = ItemId::None;
    int    amount = 0;

    // Factories
    static Job MoveTo(Vec2i t){ return Job{JobKind::MoveTo, t, {}, 0, ItemId::None, 0}; }
    static Job Chop(Vec2i t, int ticks=120){ return Job{JobKind::Chop, t, {}, ticks, ItemId::None, 0}; }
    static Job Mine(Vec2i t, int ticks=160){ return Job{JobKind::Mine, t, {}, ticks, ItemId::None, 0}; }
    static Job Haul(Vec2i from, Vec2i to, ItemId id, int qty){ return Job{JobKind::Haul, from, to, 30, id, qty}; }
    static Job Build(Vec2i t, int ticks=200){ return Job{JobKind::Build, t, {}, ticks, ItemId::None, 0}; }
    static Job Farm(Vec2i t, int ticks=100){ return Job{JobKind::Farm, t, {}, ticks, ItemId::None, 0}; }
    static Job Craft(Vec2i ws, int ticks, ItemId out=ItemId::None, int qty=0){ return Job{JobKind::Craft, ws, {}, ticks, out, qty}; }
    static Job Cook(Vec2i ws, int ticks, ItemId out=ItemId::Meal, int qty=1){ return Job{JobKind::Cook, ws, {}, ticks, out, qty}; }
    static Job Research(Vec2i ws, int ticks=200){ return Job{JobKind::Research, ws, {}, ticks, ItemId::ResearchData, 1}; }
    static Job Patrol(Vec2i a, Vec2i b, int ticks=0){ return Job{JobKind::Patrol, a, b, ticks, ItemId::None, 0}; }
    static Job Deliver(Vec2i from, Vec2i to, ItemId id, int qty){ return Job{JobKind::Deliver, from, to, 10, id, qty}; }
};

struct JobPriority {
    int p = 0; // higher = sooner
    uint64_t createdOrder = 0;  // FIFO within equal priority
};

// Replayable events
enum class EventKind : uint8_t {
    JobStarted, JobCompleted, PathFound, PathFailed, TileChanged, Debug
};
struct Event {
    EventKind kind;
    Vec2i a{}, b{};
    int   agentId = -1;
    JobKind job{};
    std::string msg;
};

