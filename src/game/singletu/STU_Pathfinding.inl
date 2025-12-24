// ============================== Pathfinding (A*) =============================

static int manhattan(Vec2i a, Vec2i b){ return std::abs(a.x-b.x)+std::abs(a.y-b.y); }

static bool neighbors4(const World& w, const Vec2i& p, std::array<Vec2i,4>& out, int& n){
    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
    n=0; for(auto d:N){ int nx=p.x+d.x, ny=p.y+d.y; if(!w.in(nx,ny)) continue; if(!w.at(nx,ny).walkable) continue; out[n++]={nx,ny}; } return n>0;
}

static bool findPathAStar(const World& w, Vec2i start, Vec2i goal, std::deque<Vec2i>& out) {
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
            if(!out.empty()) out.pop_front(); // remove start tile
            return true;
        }
        closedIx[idxOf(p)]=ci;

        neighbors4(w,p,neigh,nc);
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

