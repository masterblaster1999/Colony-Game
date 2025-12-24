// ============================== Economy & Entities ===========================

enum class Resource : uint8_t { Metal=0, Ice=1, Oxygen=2, Water=3 };
struct Stockpile { int metal=15, ice=10, oxygen=50, water=40; };

enum class BuildingKind : uint8_t { Solar=0, Habitat=1, OxyGen=2 };
struct BuildingDef {
    BuildingKind kind; Vec2i size;
    int metalCost=0, iceCost=0;
    int powerProd=0, powerCons=0;
    int oxyProd=0,  oxyCons=0;
    int waterProd=0, waterCons=0;
    int housing=0; bool needsDaylight=false;
};
static BuildingDef defSolar()  { return {BuildingKind::Solar,  {2,2},  6,0,   8,0, 0,0, 0,0, 0, true}; }
static BuildingDef defHab()    { return {BuildingKind::Habitat,{3,2}, 12,4,   0,2, 0,2, 0,2, 4, false}; }
static BuildingDef defOxyGen() { return {BuildingKind::OxyGen, {2,2}, 10,6,   2,0, 4,0, 0,0, 0, false}; }

struct Building {
    int id=0; BuildingDef def; Vec2i pos; bool powered=true;
};

struct Colony {
    Stockpile store;
    int powerBalance=0, oxygenBalance=0, waterBalance=0;
    int housing=0, population=0;
};

enum class JobType : uint8_t { None=0, MineRock=1, MineIce=2, Deliver=3, Build=4 };
struct Job { JobType type=JobType::None; Vec2i target{}; int ticks=0; int amount=0; int buildingId=0; };

struct Colonist {
    int id=0; Vec2i tile{0,0}; std::deque<Vec2i> path; Job job; int carryMetal=0; int carryIce=0;
    enum class State: uint8_t { Idle, Moving, Working } state=State::Idle;
};

