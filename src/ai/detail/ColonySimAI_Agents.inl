// ------------------------------ Agents (Colonists) ------------------------------

enum class AgentState : uint8_t { Idle, AcquireJob, Plan, Navigate, Work, Deliver, Sleep, Leisure };

struct Schedule {
    enum Block { Work, Sleep, Leisure } perHour[24];
    Schedule(){
        for(int h=0;h<24;++h) perHour[h] = Work;
        for(int h=0;h<6;++h) perHour[h] = Sleep; // 00-05
        perHour[6] = Leisure;
        for(int h=7; h<=18; ++h) perHour[h] = Work; // 07-18
        for(int h=19; h<=21; ++h) perHour[h] = Leisure;
        perHour[22] = Work;
        perHour[23] = Sleep;
    }
    Block blockAtMinute(int minuteOfDay) const {
        int h = (minuteOfDay/60)%24;
        return perHour[h];
    }
};

struct Skills {
    // simple per job skill [0..10]
    std::array<int, static_cast<size_t>(JobKind::JobKind_Count)> level{};
    int operator[](JobKind k) const { return level[static_cast<size_t>(k)]; }
    int& operator[](JobKind k) { return level[static_cast<size_t>(k)]; }
};

struct Agent {
    int id = -1;
    Vec2i pos{};
    AgentState state = AgentState::Idle;
    std::optional<Job> job;
    std::deque<Job> plan;     // upcoming jobs (GOAP / scheduler)
    Path path;
    int workLeft = 0;
    Vec2i carryTo{};          // for haul destination
    Inventory inv{8};

    // Needs (0..100; higher hunger = worse)
    int hunger = 20; // grows toward 100
    int rest = 80;   // decays toward 0
    int morale = 70;

    // Preferences
    Schedule schedule;
    Skills skills;
    int tilesPerTick = 1;
};

