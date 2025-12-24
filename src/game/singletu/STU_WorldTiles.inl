// =============================== World / Tiles ===============================

enum class TileType : uint8_t { Regolith=0, Rock=1, Ice=2, Crater=3, Sand=4 };

struct Tile {
    TileType type = TileType::Regolith;
    int resource = 0;   // ice/rock pockets
    bool walkable = true;
    uint8_t cost  = 10; // base path cost
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
                int len=r.irange(8,30), dx=(r.irange(0,1)?1:-1), dy=(r.irange(0,1)?1:-1);
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
            int X=cx+dx,Y=cy+dy; if(!in(X,Y)) continue; auto& tt=at(X,Y);
            tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
        }
    }
};

