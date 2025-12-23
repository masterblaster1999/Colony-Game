//======================================================================================
// World/Simulation (very similar to the SDL-based concept but rendered with GDI)
//======================================================================================
struct Vec2i { int x=0, y=0; bool operator==(const Vec2i& o) const { return x==o.x && y==o.y; } };
static inline Vec2i operator+(Vec2i a, Vec2i b){ return {a.x+b.x,a.y+b.y}; }
static inline Vec2i operator-(Vec2i a, Vec2i b){ return {a.x-b.x,a.y-b.y}; }
namespace std {
template<> struct hash<Vec2i> {
    size_t operator()(const Vec2i& v) const noexcept {
        return (uint64_t(uint32_t(v.x))<<32) ^ uint32_t(v.y);
    }
};
}

enum class TileType : uint8_t { Regolith=0, Rock=1, Ice=2, Crater=3, Sand=4 };
struct Tile {
    TileType type = TileType::Regolith;
    int resource = 0;
    bool walkable = true;
    uint8_t cost  = 10;
};
struct World {
    int W=120, H=80;
    std::vector<Tile> t;
    Rng* rng=nullptr;
    int idx(int x,int y) const { return y*W + x; }
    bool in(int x,int y) const { return x>=0 && y>=0 && x<W && y<H; }
    Tile& at(int x,int y){ return t[idx(x,y)]; }
    const Tile& at(int x,int y) const { return t[idx(x,y)]; }
    void resize(int w,int h){ W=w;H=h;t.assign(W*H,{}); }
    void generate(Rng& r){
        rng=&r;
        for(auto& e:t){ e.type=TileType::Regolith; e.resource=0; e.walkable=true; e.cost=10; }
        // Sand swirls
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            if(r.chance(0.015)){
                int len=r.irange(8,30), dx= (int)std::copysign(1.0, r.frand(-1,1)), dy=(int)std::copysign(1.0, r.frand(-1,1));
                int cx=x,cy=y;
                for(int i=0;i<len;++i){ if(!in(cx,cy)) break; auto& tt=at(cx,cy); tt.type=TileType::Sand; tt.cost=12; cx+=dx; cy+=dy; }
            }
        }
        // Ice pockets
        for(int k=0;k<180;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,2)){ auto& tt=at(X,Y); tt.type=TileType::Ice; tt.walkable=true; tt.cost=14; tt.resource=r.irange(5,20); }
            }
        }
        // Rock clusters
        for(int k=0;k<220;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,5);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-2,2)){ auto& tt=at(X,Y); tt.type=TileType::Rock; tt.walkable=true; tt.cost=16; tt.resource=r.irange(3,12); }
            }
        }
        // Craters
        for(int k=0;k<55;++k){
            int x=r.irange(4,W-5), y=r.irange(4,H-5), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,1)){ auto& tt=at(X,Y); tt.type=TileType::Crater; tt.walkable=false; tt.cost=255; tt.resource=0; }
            }
        }
        // HQ area
        int cx=W/2, cy=H/2;
        for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){
            int X=cx+dx,Y=cy+dy; if(!in(X,Y)) continue; auto& tt=at(X,Y); tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
        }
    }
};

// Pathfinding A* (4-neigh)
static int manhattan(Vec2i a, Vec2i b){ return std::abs(a.x-b.x)+std::abs(a.y-b.y); }
static bool neighbors(const World& w, const Vec2i& p, std::array<Vec2i,4>& out, int& n){
    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
    n=0; for(auto d:N){ int nx=p.x+d.x, ny=p.y+d.y; if(!w.in(nx,ny)) continue; if(!w.at(nx,ny).walkable) continue; out[n++]={nx,ny}; } return n>0;
}
static bool findPath(const World& w, Vec2i start, Vec2i goal, std::deque<Vec2i>& out) {
    if(!w.in(start.x,start.y)||!w.in(goal.x,goal.y)) return false;
    if(!w.at(start.x,start.y).walkable||!w.at(goal.x,goal.y).walkable) return false;

    struct Node{ Vec2i p; int g=0,f=0,parent=-1; };
    struct PQ{ int idx; int f; bool operator<(const PQ& o) const { return f>o.f; } };

    auto idxOf=[&](Vec2i p){ return p.y*w.W+p.x; };
    std::vector<Node> nodes; nodes.reserve(w.W*w.H);
    std::vector<int> openIx(w.W*w.H,-1), closedIx(w.W*w.H,-1);
    std::priority_queue<PQ> open;

    Node s; s.p=start; s.g=0; s.f=manhattan(start,goal); s.parent=-1;
    nodes.push_back(s); open.push({0,s.f}); openIx[idxOf(start)]=0;

    std::array<Vec2i,4> neigh; int nc=0;
    while(!open.empty()){
        int ci=open.top().idx; open.pop();
        Node cur=nodes[ci]; Vec2i p=cur.p;
        if(p==goal){
            std::vector<Vec2i> rev; for(int i=ci;i!=-1;i=nodes[i].parent) rev.push_back(nodes[i].p);
            out.clear(); for(int i=(int)rev.size()-1;i>=0;--i) out.push_back(rev[i]);
            if(!out.empty()) out.pop_front();
            return true;
        }
        closedIx[idxOf(p)]=ci;

        neighbors(w,p,neigh,nc);
        for(int i=0;i<nc;++i){
            Vec2i np=neigh[i]; int nid=idxOf(np);
            if(closedIx[nid]!=-1) continue;
            int step=w.at(np.x,np.y).cost; int g=cur.g+step;
            int o=openIx[nid];
            if(o==-1){
                Node n; n.p=np; n.g=g; n.f=g+manhattan(np,goal); n.parent=ci;
                o=(int)nodes.size(); nodes.push_back(n); open.push({o,n.f}); openIx[nid]=o;
            } else if(g < nodes[o].g){
                nodes[o].g=g; nodes[o].f=g+manhattan(np,goal); nodes[o].parent=ci; open.push({o,nodes[o].f});
            }
        }
    }
    return false;
}

// Colony economy and entities
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

