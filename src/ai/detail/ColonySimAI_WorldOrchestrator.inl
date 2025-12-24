// ------------------------------ World Orchestrator ------------------------------

class World {
public:
    World(int w, int h)
    : grid_(w,h), pf_(&grid_) {
        // Seed demo terrain & materials
        RNG rng;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++){
            if(rng.uniform01() < 0.02) grid_.setObstacle({x,y}, true);
            double r = rng.uniform01();
            if(r < 0.05) grid_.setMaterial({x,y}, 1);      // tree
            else if(r < 0.08) grid_.setMaterial({x,y}, 2); // rock
            grid_.setTerrainCost({x,y}, 10);
        }
        // A few stations
        buildings_.add(BuildingType::Sawmill, {w/2-3, h/2});
        buildings_.add(BuildingType::Kitchen, {w/2, h/2});
        buildings_.add(BuildingType::ResearchBench, {w/2+3, h/2});

        // Default action library
        buildActionLibrary();
        pf_.setDynamicBlocker([this](const Vec2i& p){
            return occupied_.count(p)>0;
        });
    }

    // --- Public API ---
    int spawnColonist(const Vec2i& p){
        int id = nextAgentId_++;
        Agent a; a.id=id; a.pos=p;
        for(size_t i=0;i<a.skills.level.size();++i) a.skills.level[i]=1;
        a.skills[JobKind::Chop]=3; a.skills[JobKind::Mine]=2; a.skills[JobKind::Craft]=2; a.skills[JobKind::Cook]=1;
        agents_.push_back(a);
        return id;
    }

    Grid& grid(){ return grid_; }
    const Grid& grid() const { return grid_; }
    EventBus& events(){ return bus_; }
    Stockpiles& stockpiles(){ return stockpiles_; }
    GroundItems& ground(){ return ground_; }
    BuildingManager& buildings(){ return buildings_; }
    Pathfinder& pathfinder(){ return pf_; }

    // Convenience: add a stockpile area
    uint16_t addStockpileRect(const Vec2i& a, const Vec2i& b, int priority, const std::vector<ItemId>& allow){
        uint16_t id = stockpiles_.createZone(priority);
        for(int y=std::min(a.y,b.y); y<=std::max(a.y,b.y); ++y)
            for(int x=std::min(a.x,b.x); x<=std::max(a.x,b.x); ++x){
                stockpiles_.addCell(id, {x,y});
                grid_.setZoneId({x,y}, id);
            }
        stockpiles_.setAllow(id, allow);
        return id;
    }

    // Drop ground items
    void drop(const Vec2i& p, ItemId id, int qty){ ground_.drop(p, id, qty); }

    // Save/Load
    bool save(const std::string& file){ std::ofstream f(file); if(!f) return false; saveTo(f); return true; }
    bool load(const std::string& file){ std::ifstream f(file); if(!f) return false; loadFrom(f); return true; }

    // ASCII overlay for debugging
    std::string renderAscii(int x0=0,int y0=0,int w=-1,int h=-1) const {
        if(w<0) w=grid_.width(); if(h<0) h=grid_.height();
        std::ostringstream o;
        std::unordered_set<Vec2i,Hasher> agentPos;
        for(auto& a: agents_) agentPos.insert(a.pos);
        for(int y=y0; y<y0+h && y<grid_.height(); ++y){
            for(int x=x0; x<x0+w && x<grid_.width(); ++x){
                Vec2i p{x,y};
                char c='.';
                const auto& t = grid_.at(p);
                if(!t.walkable) c='#';
                else if(agentPos.count(p)) c='@';
                else if(t.isDoor) c = t.doorOpen?'/':'|';
                else if(t.material==1) c='T';
                else if(t.material==2) c='R';
                else if(t.material==4) c='*';
                else {
                    auto it = ground_.all().find(p);
                    if(it!=ground_.all().end() && !it->second.empty()) c='i';
                    else if(t.zoneId) c='+';
                }
                o<<c;
            }
            o<<"\n";
        }
        return o.str();
    }

    // --- Main tick ---
    void update(double dt, JobQueue& externalJobs){
        timeAcc_ += dt;
        if(timeAcc_ >= tickSeconds_){
            timeAcc_ -= tickSeconds_;
            tick(externalJobs);
        }
    }

private:
    // =====================================================================
    // Core Tick
    // =====================================================================
    void tick(JobQueue& externalJobs){
        ++tickCount_;
        minuteOfDay_ = (minuteOfDay_ + 1) % 1440; // 1 minute per tick

        // Recompute occupied tiles set for pathfinder dynamic blockers
        occupied_.clear();
        for (auto& a : agents_) occupied_.insert(a.pos);

        // Auto-spawn jobs from stations if needed
        autoEnqueueWorkstationJobs(externalJobs);

        // Advance agents
        for(auto& a : agents_){
            // Needs progression
            a.hunger = std::min(100, a.hunger + 1);
            a.rest   = std::max(0, a.rest - 1);
            if(a.state==AgentState::Sleep) a.rest = std::min(100, a.rest + 3);
            if(a.state==AgentState::Leisure) a.morale = std::min(100, a.morale + 1);

            switch(a.state){
                case AgentState::Idle: handleIdle(a); break;
                case AgentState::AcquireJob: handleAcquireJob(a, externalJobs); break;
                case AgentState::Plan: handlePlan(a); break;
                case AgentState::Navigate: handleNavigate(a); break;
                case AgentState::Work: handleWork(a); break;
                case AgentState::Deliver: handleDeliver(a); break;
                case AgentState::Sleep: handleSleep(a); break;
                case AgentState::Leisure: handleLeisure(a); break;
            }
        }
    }

    // =====================================================================
    // State handlers
    // =====================================================================
    void handleIdle(Agent& a){
        // schedule-based idle behavior
        auto block = a.schedule.blockAtMinute(minuteOfDay_);
        if(block==Schedule::Sleep && a.rest<95){
            a.state = AgentState::Sleep;
            return;
        }
        if(block==Schedule::Leisure){
            a.state = AgentState::Leisure;
            return;
        }
        a.state = AgentState::AcquireJob;
    }

    void handleAcquireJob(Agent& a, JobQueue& jq){
        // if agent has a plan, use it
        if(!a.plan.empty()){
            a.job = a.plan.front(); a.plan.pop_front();
            beginJob(a);
            return;
        }

        // GOAP: if hungry, try to plan cook+eat; if low rest but schedule not sleep, skip
        if(a.hunger > 70){
            a.state = AgentState::Plan; return;
        }

        // Pull best job from queue
        if(jq.empty()){ a.state = AgentState::Idle; return; }
        auto j = jq.popBestFor(a, grid_, minuteOfDay_);
        if(!j){ a.state = AgentState::Idle; return; }

        a.job = *j;
        beginJob(a);
    }

    void handlePlan(Agent& a){
        WorldState st{a.hunger, a.rest, a.morale, a.inv.has(ItemId::Meal,1)};
        // find first applicable action (greedy)
        for(const auto& act : actions_.all()){
            if(act.pre && act.pre(a, *this, st)){
                if(act.eff) act.eff(st);
                if(act.makeJobs){
                    auto js = act.makeJobs(a, *this);
                    for(auto& j: js) a.plan.push_back(j);
                    break;
                }
            }
        }
        a.state = AgentState::AcquireJob;
    }

    void handleNavigate(Agent& a){
        if(a.path.points.empty()){
            // arrived
            if(a.job && a.pos == a.job->target){
                // Door opening if needed
                auto& t = grid_.at(a.pos);
                if(t.isDoor && !t.doorOpen){ grid_.openDoor(a.pos); bus_.publish({EventKind::TileChanged, a.pos, {}, a.id, a.job->kind, "Door opened"}); }
                a.workLeft = std::max(0, a.job->workTicks);
                a.state = AgentState::Work;
            } else {
                a.state = AgentState::Idle;
            }
            return;
        }
        Vec2i next = a.path.points.front();
        if(next == a.pos){
            a.path.points.erase(a.path.points.begin());
            if(a.path.points.empty()) return;
            next = a.path.points.front();
        }
        // move
        a.pos = next;
    }

    void handleWork(Agent& a){
        if(!a.job){ a.state = AgentState::Idle; return; }
        if(a.workLeft > 0){ --a.workLeft; return; }
        // complete
        applyJobEffect(a, *a.job);
        bus_.publish(Event{EventKind::JobCompleted, a.job->target, a.job->aux, a.id, a.job->kind});

        // For haul/deliver, may need a Deliver step
        if(a.job->kind == JobKind::Haul){
            a.carryTo = a.job->aux;
            a.path = pf_.find(a.pos, a.carryTo, tickCount_);
            a.state = a.path.empty() ? AgentState::Idle : AgentState::Deliver;
        } else {
            a.job.reset();
            a.state = AgentState::AcquireJob;
        }
    }

    void handleDeliver(Agent& a){
        if(a.path.points.empty()){
            // delivered
            // drop carried item at destination stockpile (modeled as inventory -> ground)
            if(a.job){
                int removed = a.inv.remove(a.job->item, a.job->amount);
                ground_.drop(a.pos, a.job->item, removed);
            }
            a.job.reset();
            a.state = AgentState::AcquireJob;
            return;
        }
        Vec2i next = a.path.points.front();
        if(next == a.pos){
            a.path.points.erase(a.path.points.begin());
            if(a.path.points.empty()) return;
            next = a.path.points.front();
        }
        a.pos = next;
    }

    void handleSleep(Agent& a){
        if(a.rest >= 95){ a.state = AgentState::Idle; return; }
        // chance to wake for urgent need (hunger very high)
        if(a.hunger > 90){ a.state = AgentState::Plan; return; }
    }

    void handleLeisure(Agent& a){
        // simple wander between adjacent walkable tiles
        std::array<Vec2i,4> dirs{{{1,0},{-1,0},{0,1},{0,-1}}};
        for(const auto& d : dirs){
            Vec2i np = a.pos + d;
            if(grid_.occupiable(np)){ a.pos=np; break; }
        }
        // if hunger spikes, switch to plan
        if(a.hunger>80) a.state = AgentState::Plan;
    }

    // =====================================================================
    // Job begin/apply effect
    // =====================================================================
    void beginJob(Agent& a){
        bus_.publish(Event{EventKind::JobStarted, a.job->target, a.job->aux, a.id, a.job->kind});
        // Move if needed
        if(a.pos != a.job->target){
            a.path = pf_.find(a.pos, a.job->target, tickCount_);
            if(a.path.empty()){
                bus_.publish(Event{EventKind::PathFailed, a.pos, a.job->target, a.id, a.job->kind});
                a.job.reset(); a.state = AgentState::Idle; return;
            }
            bus_.publish(Event{EventKind::PathFound, a.pos, a.job->target, a.id, a.job->kind});
            a.state = AgentState::Navigate; return;
        }
        a.workLeft = std::max(0, a.job->workTicks);
        a.state = AgentState::Work;
    }

    void applyJobEffect(Agent& a, const Job& j){
        switch(j.kind){
            case JobKind::Chop: {
                auto& t = grid_.at(j.target);
                if (t.material == 1) { t.material = 0; ground_.drop(j.target, ItemId::Log, 1); }
                break;
            }
            case JobKind::Mine: {
                auto& t = grid_.at(j.target);
                if (t.material == 2) { t.material = 0; ground_.drop(j.target, ItemId::Stone, 1); ground_.drop(j.target, ItemId::Ore, 1); }
                break;
            }
            case JobKind::Build: {
                // Mark walkable floor/road
                grid_.setObstacle(j.target, false);
                break;
            }
            case JobKind::Farm: {
                // Toggle crop
                grid_.setMaterial(j.target, 4);
                ground_.drop(j.target, ItemId::Crop, 1);
                break;
            }
            case JobKind::Haul: {
                // pick up ground items into inventory
                int got = ground_.take(j.target, j.item, j.amount);
                int left = a.inv.add(j.item, got);
                if(left>0){ ground_.drop(j.target, j.item, left); } // overflow back to ground
                break;
            }
            case JobKind::Deliver: {
                // handled in Deliver state by moving & dropping
                break;
            }
            case JobKind::Cook:
            case JobKind::Craft: {
                // consume inputs from ground on target tile (as if pre-hauled), produce outputs
                // find a workstation
                Workstation* ws=nullptr;
                for(auto& w: buildings_.all()) if(w.pos==j.target){ ws=&w; break; }
                if(ws){
                    const Recipe* rec = nullptr;
                    for(auto& r: ws->recipes) if((j.kind==JobKind::Cook ? r.jobKind==JobKind::Cook : r.jobKind==JobKind::Craft)){ rec=&r; break; }
                    if(rec){
                        // consume inputs from ground at ws
                        bool ok=true;
                        for(auto in: rec->inputs){
                            int got = ground_.take(ws->pos, in.id, in.qty);
                            if(got < in.qty){ ok=false; break; }
                        }
                        if(ok){
                            for(auto out: rec->outputs) ground_.drop(ws->pos, out.id, out.qty);
                            if(j.kind==JobKind::Cook) { a.hunger = std::max(0, a.hunger - 25); a.morale = std::min(100, a.morale + 3); }
                        }
                    }
                }
                break;
            }
            case JobKind::Research: {
                // consume paper at bench, produce research data
                ground_.take(j.target, ItemId::Paper, 1);
                ground_.drop(j.target, ItemId::ResearchData, 1);
                a.morale = std::min(100, a.morale + 2);
                break;
            }
            case JobKind::Heal: {
                if(a.inv.remove(ItemId::Medicine,1)) a.morale = std::min(100, a.morale+10);
                break;
            }
            case JobKind::Train: {
                // improve a random skill a bit
                a.skills[JobKind::Craft] = std::min(10, a.skills[JobKind::Craft]+1);
                break;
            }
            case JobKind::Tame: {
                // placeholder: morale reward
                a.morale = std::min(100, a.morale+5);
                break;
            }
            case JobKind::Patrol: {
                // enqueue move to aux and back
                break;
            }
            case JobKind::Trade: {
                // Drop some items on market tile (aux) and maybe receive others
                int removed = a.inv.remove(j.item, j.amount);
                ground_.drop(j.aux, j.item, removed);
                // receive payment (planks for logs?)
                if(j.item==ItemId::Log) ground_.drop(j.aux, ItemId::Plank, removed/2);
                break;
            }
            case JobKind::MoveTo:
            case JobKind::None: break;
            case JobKind::Deliver: break;
        }
        // notify paint
        bus_.publish(Event{EventKind::TileChanged, j.target, {}, a.id, j.kind});
    }

    // =====================================================================
    // Workstation job spawner
    // =====================================================================
    void autoEnqueueWorkstationJobs(JobQueue& jq){
        // Simple: if there's input on ground near a station and not enough output, enqueue craft/cook.
        for(auto& w: buildings_.all()){
            for(auto& r: w.recipes){
                // heuristic: if >=1 input available at ws tile, enqueue one job
                bool hasInput=true;
                for(auto in: r.inputs){
                    auto v = ground_.at(w.pos);
                    int cnt=0; if(v) for(auto& s:*v) if(s.id==in.id) cnt+=s.qty;
                    if(cnt<in.qty){ hasInput=false; break; }
                }
                if(hasInput){
                    if(r.jobKind==JobKind::Cook) jq.push(Job::Cook(w.pos, r.workTicks, r.outputs[0].id, r.outputs[0].qty), /*priority*/5);
                    else if(r.jobKind==JobKind::Research) jq.push(Job::Research(w.pos, r.workTicks), 4);
                    else jq.push(Job::Craft(w.pos, r.workTicks, r.outputs[0].id, r.outputs[0].qty), 3);
                } else {
                    // else enqueue hauls for missing inputs from nearby ground to station
                    for(auto in: r.inputs){
                        int have = 0; if(auto v=ground_.at(w.pos)) for(auto&s:*v) if(s.id==in.id) have += s.qty;
                        int need = std::max(0, in.qty - have);
                        if(need>0){
                            // find nearest tile with item
                            Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                            for(auto& kv: ground_.all()){
                                int cnt=0; for(auto&s: kv.second) if(s.id==in.id) cnt+=s.qty;
                                if(cnt>0){
                                    int d = kv.first.manhattan(w.pos);
                                    if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; }
                                }
                            }
                            if(bestQty>0){
                                int qty = std::min(bestQty, need);
                                jq.push(Job::Haul(bestPos, w.pos, in.id, qty), 6);
                            }
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // Action Library
    // =====================================================================
    void buildActionLibrary(){
        // Eat (if very hungry) — cook if meal not available, else pick up & eat
        actions_.add(GoapAction{
            "Eat",
            1,
            [](const Agent& a, const World&, const WorldState& st){
                return st.hunger > 60; // desire
            },
            [](WorldState& st){ st.hunger = std::max(0, st.hunger-40); st.hasMeal=true; },
            [](Agent& a, World& w){
                std::vector<Job> js;
                // if meal on ground at kitchen, haul to self then eat; else cook first
                Workstation* k = w.buildings().nearest(BuildingType::Kitchen, a.pos);
                if(k){
                    // ensure meal exists; if not, cook
                    int meals=0; if(auto v=w.ground().at(k->pos)) for(auto&s:*v) if(s.id==ItemId::Meal) meals+=s.qty;
                    if(meals<=0){
                        // need raw food hauled then cook
                        // haul raw food
                        Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                        for(auto& kv: w.ground().all()){
                            int cnt=0; for(auto&s: kv.second) if(s.id==ItemId::RawFood) cnt+=s.qty;
                            if(cnt>0){
                                int d = kv.first.manhattan(k->pos);
                                if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; }
                            }
                        }
                        if(bestQty>0) js.push_back(Job::Haul(bestPos, k->pos, ItemId::RawFood, 1));
                        js.push_back(Job::Cook(k->pos, 140));
                    }
                    // deliver meal to a stockpile near agent (simulate "pick up & eat")
                    js.push_back(Job::Deliver(k->pos, a.pos, ItemId::Meal, 1));
                } else {
                    // fallback: farm crop
                    js.push_back(Job::Farm(a.pos, 80));
                }
                return js;
            }
        });

        // Sleep (if schedule says sleep or very low rest)
        actions_.add(GoapAction{
            "Sleep",
            1,
            [](const Agent& a, const World&, const WorldState& st){
                (void)st;
                return a.rest < 30;
            },
            [](WorldState& st){ st.rest = std::min(100, st.rest + 60); },
            [](Agent& a, World&){ std::vector<Job> js; js.push_back(Job::MoveTo(a.pos)); return js; }
        });

        // Craft Planks at sawmill (if logs around)
        actions_.add(GoapAction{
            "CraftPlanks",
            2,
            [](const Agent& a, const World& w, const WorldState&){
                (void)a;
                // desire if logs exist anywhere
                for(auto& kv: w.ground().all())
                    for(auto&s: kv.second) if(s.id==ItemId::Log && s.qty>0) return true;
                return false;
            },
            [](WorldState& st){ st.morale = std::min(100, st.morale+1); },
            [](Agent& a, World& w){
                std::vector<Job> js;
                Workstation* s = w.buildings().nearest(BuildingType::Sawmill, a.pos);
                if(!s) return js;
                // haul log to sawmill, then craft
                Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                for(auto& kv: w.ground().all()){
                    int cnt=0; for(auto& st: kv.second) if(st.id==ItemId::Log) cnt+=st.qty;
                    if(cnt>0){ int d=kv.first.manhattan(s->pos); if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; } }
                }
                if(bestQty>0) js.push_back(Job::Haul(bestPos, s->pos, ItemId::Log, 1));
                js.push_back(Job::Craft(s->pos, 120, ItemId::Plank, 1));
                // deliver planks to nearest stockpile cell to agent
                auto dest = w.stockpiles().pickDestination(ItemId::Plank, a.pos);
                if(dest) js.push_back(Job::Deliver(s->pos, *dest, ItemId::Plank, 1));
                return js;
            }
        });

        // Research (if paper exists)
        actions_.add(GoapAction{
            "Research",
            2,
            [](const Agent&, const World& w, const WorldState&){
                for(auto& kv: w.ground().all())
                    for(auto&s: kv.second) if(s.id==ItemId::Paper && s.qty>0) return true;
                return false;
            },
            [](WorldState& st){ st.morale = std::min(100, st.morale+2); },
            [](Agent& a, World& w){
                std::vector<Job> js;
                Workstation* r = w.buildings().nearest(BuildingType::ResearchBench, a.pos);
                if(!r) return js;
                Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                for(auto& kv: w.ground().all()){
                    int cnt=0; for(auto& st: kv.second) if(st.id==ItemId::Paper) cnt+=st.qty;
                    if(cnt>0){ int d=kv.first.manhattan(r->pos); if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; } }
                }
                if(bestQty>0) js.push_back(Job::Haul(bestPos, r->pos, ItemId::Paper, 1));
                js.push_back(Job::Research(r->pos, 200));
                return js;
            }
        });

        // Patrol (walk between two points)
        actions_.add(GoapAction{
            "Patrol",
            3,
            [](const Agent&, const World&, const WorldState&){ return true; },
            [](WorldState&){},
            [](Agent& a, World& w){
                (void)w;
                std::vector<Job> js;
                Vec2i a0 = a.pos, a1 = a.pos + Vec2i{2,0};
                js.push_back(Job::Patrol(a0, a1, 0));
                js.push_back(Job::MoveTo(a0));
                return js;
            }
        });
    }

    // =====================================================================
    // Persistence
    // =====================================================================
    void saveTo(std::ostream& f){
        f << "WORLD " << grid_.width() << " " << grid_.height() << " " << minuteOfDay_ << " " << tickCount_ << "\n";
        // tiles (walkable, material, terrain, door, zone, cost)
        for(int y=0;y<grid_.height();++y){
            for(int x=0;x<grid_.width();++x){
                const auto& t = grid_.at({x,y});
                f << "T " << x << " " << y << " " << t.walkable << " " << int(t.material) << " " << int(t.terrain)
                  << " " << t.isDoor << " " << t.doorOpen << " " << t.zoneId << " " << t.moveCost << "\n";
            }
        }
        // agents
        for(const auto& a : agents_){
            f << "A " << a.id << " " << a.pos.x << " " << a.pos.y << " " << int(a.state)
              << " " << a.hunger << " " << a.rest << " " << a.morale << " " << a.inv.capacity() << "\n";
            for(auto& s: a.inv.slots()) f << "AS " << int(s.id) << " " << s.qty << "\n";
        }
        // ground items
        for(auto& kv : ground_.all()){
            for(auto& s : kv.second){
                f << "G " << kv.first.x << " " << kv.first.y << " " << int(s.id) << " " << s.qty << "\n";
            }
        }
        // stockpiles
        for(auto& z : stockpiles_.zones()){
            f << "Z " << z.id << " " << z.priority << "\n";
            for(auto i : z.allow) f << "ZA " << z.id << " " << int(i) << "\n";
            for(auto c : z.cells) f << "ZC " << z.id << " " << c.x << " " << c.y << "\n";
        }
        // workstations
        int idx=0;
        for(auto& w: buildings_.all()){
            f << "W " << idx++ << " " << int(w.type) << " " << w.pos.x << " " << w.pos.y << "\n";
        }
    }

    void loadFrom(std::istream& f){
        // reset
        agents_.clear(); ground_.mut().clear();
        stockpiles_ = Stockpiles();
        buildings_ = BuildingManager();

        std::string tok; int W=0,H=0;
        int invCap=0;
        while(f >> tok){
            if(tok=="WORLD"){
                f >> W >> H >> minuteOfDay_ >> tickCount_;
                grid_ = Grid(W,H);
                pf_ = Pathfinder(&grid_);
                pf_.setDynamicBlocker([this](const Vec2i& p){ return occupied_.count(p)>0; });
            } else if(tok=="T"){
                int x,y; bool walk; int mat, ter; bool isDoor, open; int zid; int cost;
                f >> x >> y >> walk >> mat >> ter >> isDoor >> open >> zid >> cost;
                auto& t = grid_.at({x,y});
                t.walkable = walk; t.material=uint8_t(mat); t.terrain=uint8_t(ter);
                t.isDoor=isDoor; t.doorOpen=open; t.zoneId=uint16_t(zid); t.moveCost=uint16_t(cost);
            } else if(tok=="A"){
                Agent a;
                int st; f >> a.id >> a.pos.x >> a.pos.y >> st >> a.hunger >> a.rest >> a.morale >> invCap;
                a.state = static_cast<AgentState>(st);
                a.inv = Inventory(invCap);
                agents_.push_back(std::move(a));
            } else if(tok=="AS"){
                if(agents_.empty()) continue;
                int id, qty; f >> id >> qty;
                agents_.back().inv.add(static_cast<ItemId>(id), qty);
            } else if(tok=="G"){
                int x,y,id,qty; f >> x >> y >> id >> qty; ground_.drop({x,y}, static_cast<ItemId>(id), qty);
            } else if(tok=="Z"){
                uint16_t id; int pri; f >> id >> pri;
                // create exact id: hacky (create new and set id)
                uint16_t newId = stockpiles_.createZone(pri);
                (void)newId; // id may differ, but we'll set cells/allow using provided id mapping if needed
                // (for simplicity, we ignore preserving exact zone IDs)
            } else if(tok=="ZA"){
                uint16_t id; int item; f >> id >> item; (void)id; (void)item;
                // simplified: ignoring allow reconstruction mapping here to keep loader concise
            } else if(tok=="ZC"){
                uint16_t id; int x,y; f >> id >> x >> y; (void)id; (void)x; (void)y;
                // simplified loader: not reconstructing zones exactly in this compact example
            } else if(tok=="W"){
                int idx,type,x,y; f >> idx >> type >> x >> y;
                buildings_.add(static_cast<BuildingType>(type), Vec2i{x,y});
            }
        }
    }

private:
    // ---------------------- Members ----------------------
    Grid grid_{1,1};
    Pathfinder pf_{&grid_};
    EventBus bus_;
    std::vector<Agent> agents_;
    Stockpiles stockpiles_;
    GroundItems ground_;
    BuildingManager buildings_;
    ActionLibrary actions_;

    // Time
    double timeAcc_ = 0.0;
    const double tickSeconds_ = 0.1; // 10 ticks / sec
    uint64_t tickCount_ = 0;
    int minuteOfDay_ = 8*60; // start at 08:00

    // Pathfinding dynamic blockers
    std::unordered_set<Vec2i,Hasher> occupied_;

    int nextAgentId_ = 1;
};

// ============================ End namespace ============================
} // namespace colony
