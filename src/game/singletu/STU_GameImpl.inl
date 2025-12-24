// ================================ Game Impl =================================

static const wchar_t* kWndClass = L"ColonyGame_SingleTU_Win32";
static const wchar_t* kWndTitle = L"Colony Game";

class Game {
public:
    Game(HINSTANCE hInst, const GameOptions& opts)
        : hInst_(hInst), opts_(opts), rng_(opts.seed ? opts.seed : kDefaultSeed) {}

    int Run() {
        if (!CreateMainWindow()) return 3;
        InitWorld();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        util::Timer timer;
        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running_=false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            double dt = timer.Tick();
            if (!paused_) {
                simAcc_ += dt * simSpeed_;
                if (simAcc_ > 0.5) simAcc_ = 0.5;          // avoid spiral after long pause
                while (simAcc_ >= fixedDt_) {
                    Update(fixedDt_);
                    simAcc_ -= fixedDt_;
                }
            }

            Render();
            if (opts_.vsync) Sleep(1);                    // crude vsync-ish
        }
        return 0;
    }

private:
    // ---------------- Window / WndProc ----------------
    static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        Game* self = reinterpret_cast<Game*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(h, m, w, l);
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->WndProc(h, m, w, l);
    }

    bool CreateMainWindow() {
        WNDCLASSW wc{}; wc.hInstance=hInst_; wc.lpfnWndProc=StaticWndProc;
        wc.hCursor=LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=kWndClass;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        if (!RegisterClassW(&wc)) return false;

        if (opts_.fullscreen) {
            // Borderless fullscreen on primary monitor
            hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWndClass, kWndTitle, WS_POPUP,
                0,0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                nullptr, nullptr, hInst_, this);
        } else {
            DWORD style = WS_OVERLAPPEDWINDOW;
            RECT rc{0,0,(LONG)opts_.width,(LONG)opts_.height};
            AdjustWindowRect(&rc, style, FALSE);
            int W = rc.right-rc.left, H = rc.bottom-rc.top;
            hwnd_ = CreateWindowExW(0, kWndClass, kWndTitle, style,
                CW_USEDEFAULT, CW_USEDEFAULT, W, H, nullptr, nullptr, hInst_, this);
        }
        if (!hwnd_) return false;

        // HUD font
        LOGFONTW lf{};
        HDC tmpdc = GetDC(hwnd_);
        lf.lfHeight = -MulDiv(10, GetDeviceCaps(tmpdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd_, tmpdc);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf);
        return true;
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_SIZE: {
            clientW_ = LOWORD(l); clientH_ = HIWORD(l);
            HDC hdc = GetDC(h);
            if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);
            ReleaseDC(h, hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx=GET_X_LPARAM(l), my=GET_Y_LPARAM(l);
            OnLeftClick(mx,my);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            buildMode_ = false; selected_ = std::nullopt;
            return 0;
        }
        case WM_MOUSEWHEEL: {
            short z = GET_WHEEL_DELTA_WPARAM(w);
            if (z>0) zoom_ = util::clamp(zoom_ * 1.1, 0.5, 2.5);
            else     zoom_ = util::clamp(zoom_ / 1.1, 0.5, 2.5);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (w) {
                case VK_ESCAPE:
                    if (buildMode_) { buildMode_=false; selected_.reset(); }
                    else { running_=false; }
                    break;
                case 'P': paused_ = !paused_; break;
                case VK_OEM_PLUS: case VK_ADD: simSpeed_=util::clamp(simSpeed_*1.25,0.25,8.0); break;
                case VK_OEM_MINUS: case VK_SUBTRACT: simSpeed_=util::clamp(simSpeed_/1.25,0.25,8.0); break;
                case '1': selected_=BuildingKind::Solar;  buildMode_=true;  break;
                case '2': selected_=BuildingKind::Habitat;buildMode_=true;  break;
                case '3': selected_=BuildingKind::OxyGen; buildMode_=true;  break;
                case 'G': SpawnColonist(); break;
                case 'B': { auto t=MouseToTile(lastMouse_); Bulldoze(t); } break;
                case VK_LEFT:  keyPan_.x=-1; break;
                case VK_RIGHT: keyPan_.x=+1; break;
                case VK_UP:    keyPan_.y=-1; break;
                case VK_DOWN:  keyPan_.y=+1; break;
                case 'S': SaveGame(); break;
                case 'L': LoadGame(); break;
            }
            return 0;
        }
        case WM_KEYUP: {
            switch (w) {
                case VK_LEFT:  if (keyPan_.x==-1) keyPan_.x=0; break;
                case VK_RIGHT: if (keyPan_.x==+1) keyPan_.x=0; break;
                case VK_UP:    if (keyPan_.y==-1) keyPan_.y=0; break;
                case VK_DOWN:  if (keyPan_.y==+1) keyPan_.y=0; break;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            lastMouse_.x = GET_X_LPARAM(l);
            lastMouse_.y = GET_Y_LPARAM(l);
            return 0;
        }
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    // ---------------- World / Sim init ----------------
    void InitWorld() {
        // Log
        std::wstring logDir = util::Widen(opts_.saveDir);
        if (!logDir.empty()) {
            std::wstring logs = util::JoinPath(logDir.substr(0, logDir.find(L"\\Saves")), L"Logs");
            util::EnsureDir(logs);
            g_log.Open(util::JoinPath(logs, L"Game-" + util::NowStampCompact() + L".log"));
        }
        g_log.Line(L"Game init…");

        tileSize_ = 24;
        world_.resize(120, 80);
        world_.generate(rng_);

        hq_ = { world_.W/2, world_.H/2 };
        TryPlaceImmediate(BuildingKind::Solar,  hq_ + Vec2i{3,-2});
        TryPlaceImmediate(BuildingKind::Habitat,hq_ + Vec2i{3, 0});
        TryPlaceImmediate(BuildingKind::OxyGen, hq_ + Vec2i{0, 3});

        // Center camera
        camera_.x = (hq_.x*tileSize_) - clientW_/2;
        camera_.y = (hq_.y*tileSize_) - clientH_/2;

        SpawnColonist();
        Banner(L"Welcome to Mars — profile: " + util::Widen(opts_.profile));
    }

    void SpawnColonist() {
        Colonist c; c.id = nextColonistId_++; c.tile = hq_; colonists_.push_back(c);
        Banner(L"Colonist arrived");
    }

    // ---------------- Input helpers ------------------
    Vec2i MouseToTile(POINT p) const {
        int wx = int(camera_.x + p.x/zoom_);
        int wy = int(camera_.y + p.y/zoom_);
        return { wx / tileSize_, wy / tileSize_ };
    }
    void OnLeftClick(int mx, int my) {
        POINT p{mx,my};
        if (buildMode_ && selected_.has_value()) {
            Vec2i t = MouseToTile(p);
            TryQueueBuild(*selected_, t);
            buildMode_=false; selected_.reset();
            return;
        }
    }

    // ---------------- Build placement ----------------
    BuildingDef Def(BuildingKind k) {
        switch(k) {
            case BuildingKind::Solar:  return defSolar();
            case BuildingKind::Habitat:return defHab();
            case BuildingKind::OxyGen: return defOxyGen();
        }
        return defSolar();
    }
    bool CheckFootprint(const BuildingDef& d, Vec2i topLeft) {
        for(int dy=0;dy<d.size.y;++dy) for(int dx=0;dx<d.size.x;++dx) {
            int x=topLeft.x+dx, y=topLeft.y+dy;
            if(!world_.in(x,y)) return false;
            const Tile& t = world_.at(x,y);
            if(!t.walkable || t.type==TileType::Crater) return false;
        }
        return true;
    }
    void Bulldoze(Vec2i t) {
        if(!world_.in(t.x,t.y)) return;
        auto& tt=world_.at(t.x,t.y);
        tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
    }
    bool TryQueueBuild(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) { Banner(L"Invalid location"); return false; }
        if (colony_.store.metal < d.metalCost || colony_.store.ice < d.iceCost) { Banner(L"Not enough resources"); return false; }
        pendingBuild_ = Building{ nextBuildingId_++, d, topLeft, true };
        Banner(L"Construction queued: " + NameOf(k));
        return true;
    }
    void TryPlaceImmediate(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) return;
        buildings_.push_back(Building{ nextBuildingId_++, d, topLeft, true });
    }

    // ---------------- Update loop --------------------
    void Update(double dt) {
        // Camera pan
        const double pan=300.0;
        camera_.x += keyPan_.x * pan * dt;
        camera_.y += keyPan_.y * pan * dt;

        // Day/night
        dayTime_ += dt*0.02;
        if (dayTime_>=1.0) dayTime_ -= 1.0;

        EconomyTick();
        AITick();
    }
    void EconomyTick() {
        colony_.powerBalance = colony_.oxygenBalance = colony_.waterBalance = 0;
        colony_.housing = 0;
        bool daylight = (dayTime_>0.1 && dayTime_<0.9);
        for(auto& b:buildings_) {
            b.powered = true;
            if (b.def.needsDaylight && !daylight) { /* no solar output */ }
            else colony_.powerBalance += b.def.powerProd;
            colony_.powerBalance -= b.def.powerCons;

            colony_.oxygenBalance += b.def.oxyProd;
            colony_.oxygenBalance -= b.def.oxyCons;
            colony_.waterBalance  += b.def.waterProd;
            colony_.waterBalance  -= b.def.waterCons;
            colony_.housing       += b.def.housing;
        }
        colony_.store.oxygen = std::max(0, colony_.store.oxygen + colony_.oxygenBalance);
        colony_.store.water  = std::max(0, colony_.store.water  + colony_.waterBalance);
        int people = (int)colonists_.size();
        if (people>0) {
            colony_.store.oxygen = std::max(0, colony_.store.oxygen - people);
            colony_.store.water  = std::max(0, colony_.store.water  - people);
        }
        colony_.population=people;
    }
    void AITick() {
        for (auto& c:colonists_) {
            switch (c.state) {
                case Colonist::State::Idle:    AIIdle(c); break;
                case Colonist::State::Moving:  AIMove(c); break;
                case Colonist::State::Working: AIWork(c); break;
            }
        }
    }
    void AIIdle(Colonist& c) {
        if (pendingBuild_.has_value()) {
            // Go adjacent to footprint
            std::vector<Vec2i> opts;
            for(int dy=0;dy<pendingBuild_->def.size.y;++dy)
                for(int dx=0;dx<pendingBuild_->def.size.x;++dx) {
                    Vec2i p = pendingBuild_->pos + Vec2i{dx,dy};
                    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
                    for(auto d:N){ Vec2i n=p+d; if(world_.in(n.x,n.y)&&world_.at(n.x,n.y).walkable) opts.push_back(n); }
                }
            if(!opts.empty()){
                Vec2i pick = opts[rng_.irange(0,(int)opts.size()-1)];
                std::deque<Vec2i> path;
                if (findPathAStar(world_, c.tile, pick, path)) {
                    c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Build,pendingBuild_->pos,18,0,pendingBuild_->id};
                    return;
                }
            }
        }
        if (colony_.store.oxygen < 40) if (TryAssignMining(c, TileType::Ice)) return;
        if (TryAssignMining(c, TileType::Rock)) return;
        // wander to HQ
        if (c.tile != hq_) {
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,0,0};}
        }
    }
    bool TryAssignMining(Colonist& c, TileType tt) {
        int bestD = INT32_MAX; Vec2i best{-1,-1};
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x){
            const Tile& t=world_.at(x,y);
            if (t.type==tt && t.resource>0 && t.walkable) {
                int d=manhattan(c.tile,{x,y});
                if(d<bestD){bestD=d; best={x,y};}
            }
        }
        if (best.x>=0) {
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, best, path)) {
                c.path=std::move(path); c.state=Colonist::State::Moving;
                c.job={ (tt==TileType::Ice)?JobType::MineIce:JobType::MineRock, best, 18, 0, 0 };
                return true;
            }
        }
        return false;
    }
    void AIMove(Colonist& c) {
        moveAcc_ += fixedDt_;
        const double step=0.12;
        if (moveAcc_>=step && !c.path.empty()) {
            c.tile=c.path.front(); c.path.pop_front();
            moveAcc_-=step;
            if (c.path.empty()) { c.state=Colonist::State::Working; c.job.ticks=18; }
        }
    }
    void AIWork(Colonist& c) {
        if (c.job.ticks>0) { --c.job.ticks; return; }
        if (c.job.type==JobType::MineIce || c.job.type==JobType::MineRock) {
            Tile& t=world_.at(c.job.target.x,c.job.target.y);
            int mined = std::min(3, t.resource);
            if (mined<=0){ c.state=Colonist::State::Idle; return; }
            t.resource -= mined;
            if (c.job.type==JobType::MineIce) c.carryIce += mined; else c.carryMetal += mined;
            std::deque<Vec2i> path; if(findPathAStar(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,mined,0};}
            else c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Deliver) {
            colony_.store.metal += c.carryMetal; c.carryMetal=0;
            colony_.store.ice   += c.carryIce;   c.carryIce=0;
            c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Build) {
            if (pendingBuild_.has_value() && pendingBuild_->id==c.job.buildingId) {
                if (colony_.store.metal >= pendingBuild_->def.metalCost &&
                    colony_.store.ice   >= pendingBuild_->def.iceCost) {
                    colony_.store.metal -= pendingBuild_->def.metalCost;
                    colony_.store.ice   -= pendingBuild_->def.iceCost;
                    buildings_.push_back(*pendingBuild_);
                    pendingBuild_.reset();
                }
            }
            c.state=Colonist::State::Idle;
        } else {
            c.state=Colonist::State::Idle;
        }
    }

    // ---------------- Save / Load --------------------
    void SaveGame() {
        if (opts_.saveDir.empty()) { Banner(L"Save dir not set"); return; }
        std::wstring saveDir = util::Widen(opts_.saveDir);
        util::EnsureDir(saveDir);
        std::wstring file = util::JoinPath(saveDir, util::Widen(opts_.profile) + L".save");
        std::ofstream out(file, std::ios::out | std::ios::trunc);
        if (!out) { Banner(L"Save failed"); return; }
        out << "MCS_SAVE v1\n";
        out << "seed " << opts_.seed << "\n";
        out << "world " << world_.W << " " << world_.H << "\n";
        out << "hq " << hq_.x << " " << hq_.y << "\n";
        out << "store " << colony_.store.metal << " " << colony_.store.ice << " "
            << colony_.store.oxygen << " " << colony_.store.water << "\n";
        out << "buildings " << buildings_.size() << "\n";
        for (auto& b : buildings_) {
            out << int(b.def.kind) << " " << b.pos.x << " " << b.pos.y << "\n";
        }
        if (pendingBuild_.has_value()) {
            out << "pending 1 " << int(pendingBuild_->def.kind) << " "
                << pendingBuild_->pos.x << " " << pendingBuild_->pos.y << " " << pendingBuild_->id << "\n";
        } else out << "pending 0\n";
        out << "colonists " << colonists_.size() << "\n";
        for (auto& c : colonists_) {
            out << c.id << " " << c.tile.x << " " << c.tile.y << "\n";
        }
        Banner(L"Game saved");
    }

    void LoadGame() {
        if (opts_.saveDir.empty()) { Banner(L"Save dir not set"); return; }
        std::wstring file = util::JoinPath(util::Widen(opts_.saveDir), util::Widen(opts_.profile) + L".save");
        std::ifstream in(file);
        if (!in) { Banner(L"No save"); return; }

        std::string header; in >> header; if (header != "MCS_SAVE") { Banner(L"Bad save"); return; }
        std::string tag; in >> tag; /* v1 */

        uint64_t seedIn=0; in >> tag; if (tag!="seed") { Banner(L"Load fail: seed"); return; } in >> seedIn;
        int W,H; in >> tag; if (tag!="world") { Banner(L"Load fail: world"); return; } in >> W >> H; world_.resize(W,H); world_.generate(rng_);
        in >> tag; if (tag!="hq") { Banner(L"Load fail: hq"); return; } in >> hq_.x >> hq_.y;
        in >> tag; if (tag!="store") { Banner(L"Load fail: store"); return; }
        in >> colony_.store.metal >> colony_.store.ice >> colony_.store.oxygen >> colony_.store.water;

        in >> tag; if (tag!="buildings") { Banner(L"Load fail: buildings"); return; }
        size_t bc; in >> bc; buildings_.clear();
        for (size_t i=0;i<bc;++i) {
            int kind,x,y; in >> kind >> x >> y;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            buildings_.push_back(Building{ nextBuildingId_++, def, {x,y}, true});
        }

        in >> tag; if (tag!="pending") { Banner(L"Load fail: pending"); return; }
        int hasPending; in >> hasPending;
        if (hasPending==1) {
            int kind,x,y,id; in >> kind >> x >> y >> id;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            pendingBuild_ = Building{id, def, {x,y}, true};
        } else pendingBuild_.reset();

        in >> tag; if (tag!="colonists") { Banner(L"Load fail: colonists"); return; }
        size_t cc; in >> cc; colonists_.clear();
        for (size_t i=0;i<cc;++i) {
            Colonist c; in >> c.id >> c.tile.x >> c.tile.y;
            colonists_.push_back(c);
            nextColonistId_ = std::max(nextColonistId_, c.id+1);
        }
        Banner(L"Game loaded");
    }

    // ---------------- Rendering ----------------------
    void Render() {
        HDC hdc = GetDC(hwnd_);
        if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);

        // Mars-ish sky based on dayTime_
        double daylight = std::cos((dayTime_-0.5)*3.14159*2.0)*0.5+0.5;
        int R=int(120+70*daylight), G=int(40+30*daylight), B=int(35+25*daylight);
        HBRUSH sky = CreateSolidBrush(RGB(R,G,B));
        RECT full{0,0,clientW_,clientH_}; FillRect(back_.mem, &full, sky); DeleteObject(sky);

        DrawWorld();
        DrawBuildings();
        DrawColonists();
        if (buildMode_ && selected_.has_value()) DrawPlacement(*selected_);
        DrawHQ();
        DrawHUD();

        BitBlt(hdc, 0,0, clientW_,clientH_, back_.mem, 0,0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);
    }

    void DrawWorld() {
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x) {
            const Tile& t=world_.at(x,y);
            COLORREF c = RGB(139,85,70);
            switch(t.type){
                case TileType::Regolith: c=RGB(139,85,70); break;
                case TileType::Sand:     c=RGB(168,120,85); break;
                case TileType::Ice:      c=RGB(120,170,200); break;
                case TileType::Rock:     c=RGB(100,100,110); break;
                case TileType::Crater:   c=RGB(40,40,45); break;
            }
            DrawCell(x,y,c);
            // subtle grid outline
            HPEN pen=CreatePen(PS_SOLID,1,RGB(0,0,0)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
            RECT rc = TileRect(x,y); MoveToEx(back_.mem, rc.left, rc.top, nullptr);
            LineTo(back_.mem, rc.right, rc.top); LineTo(back_.mem, rc.right, rc.bottom); LineTo(back_.mem, rc.left, rc.bottom); LineTo(back_.mem, rc.left, rc.top);
            SelectObject(back_.mem, old); DeleteObject(pen);
        }
    }
    void DrawBuildings() {
        for(auto& b:buildings_){
            COLORREF col = (b.def.kind==BuildingKind::Solar)?RGB(60,120,200):
                           (b.def.kind==BuildingKind::Habitat)?RGB(200,160,80):RGB(90,200,140);
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(col); FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        if (pendingBuild_.has_value()) {
            auto& b=*pendingBuild_;
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(RGB(255,255,255));
            FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
    }
    void DrawColonists() {
        for(auto& c:colonists_) {
            RECT rc = TileRect(c.tile.x,c.tile.y);
            HBRUSH br=CreateSolidBrush(RGB(240,90,70)); FillRect(back_.mem,&rc,br); DeleteObject(br);

            if (!c.path.empty()) {
                HPEN pen=CreatePen(PS_SOLID,2,RGB(30,220,255)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
                Vec2i prev=c.tile;
                for(auto p:c.path){
                    RECT a=TileRect(prev.x,prev.y), b=TileRect(p.x,p.y);
                    int ax=(a.left+a.right)/2, ay=(a.top+a.bottom)/2;
                    int bx=(b.left+b.right)/2, by=(b.top+b.bottom)/2;
                    MoveToEx(back_.mem, ax, ay, nullptr); LineTo(back_.mem, bx, by);
                    prev=p;
                }
                SelectObject(back_.mem, old); DeleteObject(pen);
            }
        }
    }
    void DrawPlacement(BuildingKind k) {
        Vec2i t = MouseToTile(lastMouse_);
        auto d = Def(k);
        bool ok = CheckFootprint(d,t);
        RECT rc = TileRect(t.x,t.y);
        rc.right  = rc.left + int(d.size.x * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(d.size.y * tileSize_ * zoom_);
        HBRUSH br = CreateSolidBrush(ok?RGB(100,255,100):RGB(255,80,80));
        FillRect(back_.mem,&rc,br); DeleteObject(br);
        FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        std::wstringstream tip;
        tip << NameOf(k) << L"  M:" << d.metalCost << L" I:" << d.iceCost;
        DrawTooltip(lastMouse_.x+14, lastMouse_.y+14, tip.str());
    }
    void DrawHQ() {
        RECT rc = TileRect(hq_.x,hq_.y);
        rc.right  = rc.left + int(2 * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(2 * tileSize_ * zoom_);
        HBRUSH br=CreateSolidBrush(RGB(200,80,120)); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    void DrawHUD() {
        int pad=8, w=560, h=116;
        RECT hud{pad,pad,pad+w,pad+h};
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&hud,bg); DeleteObject(bg);
        FrameRect(back_.mem,&hud,(HBRUSH)GetStockObject(BLACK_BRUSH));

        HFONT oldFont = (HFONT)SelectObject(back_.mem, font_);
        SetBkMode(back_.mem, TRANSPARENT);
        SetTextColor(back_.mem, RGB(230,230,240));

        int x=hud.left+8, y=hud.top+6;
        std::wstringstream l1; l1<<L"Time "<<std::fixed<<std::setprecision(2)<<dayTime_<<L"   x"<<std::setprecision(2)<<simSpeed_<<(paused_?L"  [PAUSED]":L"");
        DrawTextLine(x,y,l1.str()); y+=16;

        std::wstringstream r1; r1<<L"Metal "<<colony_.store.metal<<L"   Ice "<<colony_.store.ice<<L"   O2 "<<colony_.store.oxygen<<L"   H2O "<<colony_.store.water;
        DrawTextLine(x,y,r1.str()); y+=16;

        std::wstringstream r2; r2<<L"Power "<<colony_.powerBalance<<L"   O2 "<<colony_.oxygenBalance<<L"   H2O "<<colony_.waterBalance<<L"   Pop "<<colony_.population<<L"/"<<colony_.housing;
        DrawTextLine(x,y,r2.str()); y+=16;

        std::wstring sel = selected_.has_value()? NameOf(*selected_) : L"None";
        DrawTextLine(x,y, L"Build: "+sel); y+=16;

        SetTextColor(back_.mem, RGB(255,128,64));
        DrawTextLine(x,y, L"1=Solar  2=Hab  3=O2Gen   LMB place  RMB cancel  G colonist  S/L save/load  P pause  +/- speed  Arrows pan");

        SelectObject(back_.mem, oldFont);

        if (!banner_.empty() && bannerTime_>0.0) {
            int bw = (int)banner_.size()*8+24; int bh=24;
            RECT b{ (clientW_-bw)/2, clientH_-bh-12, (clientW_+bw)/2, clientH_-12 };
            HBRUSH bb=CreateSolidBrush(RGB(30,30,35)); FillRect(back_.mem,&b,bb); DeleteObject(bb);
            FrameRect(back_.mem, &b, (HBRUSH)GetStockObject(BLACK_BRUSH));
            HFONT of=(HFONT)SelectObject(back_.mem,font_);
            SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(255,255,255));
            RECT trc=b; trc.left+=12; trc.top+=4; DrawTextW(back_.mem, banner_.c_str(), -1, &trc, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(back_.mem,of);
            bannerTime_ -= 0.016;
            if (bannerTime_<=0.0) banner_.clear();
        }
    }

    void DrawTextLine(int x, int y, const std::wstring& s) {
        RECT rc{ x,y,x+1000,y+16 };
        DrawTextW(back_.mem, s.c_str(), -1, &rc, DT_LEFT|DT_TOP|DT_SINGLELINE);
    }

    void DrawTooltip(int x, int y, const std::wstring& text) {
        RECT rc{ x,y,x+(int)text.size()*8+8, y+20 };
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&rc,bg); DeleteObject(bg);
        FrameRect(back_.mem,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HFONT of=(HFONT)SelectObject(back_.mem,font_);
        SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(230,230,240));
        RECT t=rc; t.left+=4; t.top+=2; DrawTextW(back_.mem, text.c_str(), -1, &t, DT_LEFT|DT_TOP|DT_SINGLELINE);
        SelectObject(back_.mem,of);
    }

    RECT TileRect(int tx,int ty) const {
        int px = int((tx*tileSize_ - camera_.x) * zoom_);
        int py = int((ty*tileSize_ - camera_.y) * zoom_);
        int s  = int(tileSize_ * zoom_);
        RECT rc{px,py,px+s,py+s};
        return rc;
    }
    void DrawCell(int x,int y, COLORREF c) {
        RECT rc = TileRect(x,y);
        HBRUSH br=CreateSolidBrush(c); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    std::wstring NameOf(BuildingKind k) {
        switch(k){
            case BuildingKind::Solar:  return L"Solar Panel";
            case BuildingKind::Habitat:return L"Habitat";
            case BuildingKind::OxyGen: return L"Oxygen Generator";
        }
        return L"?";
    }
    void Banner(const std::wstring& s) { banner_ = s; bannerTime_ = 3.0; }

private:
    // Win
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    BackBuffer back_;
    HFONT font_ = nullptr;
    int clientW_=1280, clientH_=720;

    // Camera
    struct { double x=0,y=0; } camera_;
    double zoom_=1.0;

    // Options
    GameOptions opts_;

    // World
    World world_;
    Rng   rng_;
    int   tileSize_=24;
    Vec2i hq_{0,0};
    std::vector<Building> buildings_;
    std::optional<Building> pendingBuild_;
    int nextBuildingId_=1;

    std::vector<Colonist> colonists_;
    int nextColonistId_=1;

    Colony colony_;

    // Sim
    bool running_=true, paused_=false;
    double simSpeed_=1.0;
    const double fixedDt_=1.0/60.0;
    double simAcc_=0.0, moveAcc_=0.0, dayTime_=0.25;

    // Input state
    Vec2i keyPan_{0,0};
    bool buildMode_=false; std::optional<BuildingKind> selected_;
    POINT lastMouse_{};

    // Banner
    std::wstring banner_; double bannerTime_=0.0;
};

