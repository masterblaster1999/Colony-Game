
//======================================================================================
// Game application (Win32)
//======================================================================================
static const wchar_t* kWndClass = L"ColonyGame_Win32_Class";
static const wchar_t* kWndTitle = L"Colony Game (Win32)";

class GameApp {
public:
    GameApp(HINSTANCE hInst, const AppPaths& paths, const Config& cfg)
        : hInst_(hInst), paths_(paths), cfg_(cfg),
          // Robust RNG seeding: remove invalid literal suffix and use a solid default
          rng_(cfg.seed.value_or(0x9E3779B97F4A7C15ull)) {}

    int Run() {
        if (!CreateMainWindow()) return 3;
        InitWorld();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        // Initialize XInput (dynamic loader + first-pad discovery)
        InitGamepad();

        Timer timer;
        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running_=false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            double dt = timer.Tick();

            // Poll controller once per frame (affects camera pan/zoom and actions)
            PollGamepad(dt);

            if (!paused_) {
                simAcc_ += dt * simSpeed_;
                if (simAcc_ > 0.5) simAcc_ = 0.5;
                while (simAcc_ >= fixedDt_) {
                    Update(fixedDt_);
                    simAcc_ -= fixedDt_;
                }
            }

            Render();
            if (cfg_.vsync) { // crude vsync-ish
                Sleep(1);
            }
        }
        return 0;
    }

private:
    // ------------------ Window / WndProc ------------------
    static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        GameApp* self = reinterpret_cast<GameApp*>(GetWindowLongPtrW(h, GWLP_USERDATA));
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

        // Always create as overlapped; we'll apply borderless/fullscreen after creation.
        DWORD style = WS_OVERLAPPEDWINDOW;

        RECT rc{0,0,(LONG)cfg_.width,(LONG)cfg_.height};

        // DPI-aware rect sizing for the initial client area using AdjustWindowRectExForDpi. :contentReference[oaicite:4]{index=4}
        UINT initDpi = 96;
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            using GetDpiForSystemFn = UINT (WINAPI*)(void);
            if (auto p = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"))) {
                initDpi = p();
            }
        }
        AdjustWindowRectExForDpi(&rc, style, FALSE, 0, initDpi);

        int W = rc.right-rc.left, H = rc.bottom-rc.top;

        hwnd_ = CreateWindowExW(0, kWndClass, kWndTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT, W, H, nullptr, nullptr, hInst_, this);
        if (!hwnd_) return false;

        // Cache original styles right after creation so we can restore later.
        origStyle_  = (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE);
        origExStyle_= (DWORD)GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

        // DPI-aware font for HUD using the window's current DPI
        UINT dpi = 0;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
            auto pGet = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            if (pGet) dpi = pGet(hwnd_); // Win10+ API
        }
        dpi_ = (dpi != 0) ? dpi : 96;
        LOGFONTW lf{}; lf.lfHeight = -MulDiv(10, (int)dpi_, 96);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf);

        // NEW: Register for raw mouse input (WM_INPUT). Keep legacy WM_MOUSE* messages.
        if (cfg_.rawInput) {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01; // Generic Desktop
            rid.usUsage     = 0x02; // Mouse
            rid.dwFlags     = 0;    // do not suppress legacy mouse messages
            rid.hwndTarget  = hwnd_;
            if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
                cfg_.rawInput = false; // fall back silently
            }
        }

        // If config asked for fullscreen, apply our borderless mode now.
        if (cfg_.fullscreen) {
            ApplyWindowMode(/*borderless*/true);
        }

        return true;
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_SYSKEYDOWN: {
            // Alt+Enter toggles borderless fullscreen (only when ALT is truly down). :contentReference[oaicite:5]{index=5}
            if (w == VK_RETURN && (HIWORD(l) & KF_ALTDOWN)) { ToggleBorderless(); return 0; }
            break;
        }
        case WM_GETMINMAXINFO: {
            // Keep a sensible minimum size so we don't thrash GDI resources on very small windows.
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(l);
            mmi->ptMinTrackSize.x = 640;
            mmi->ptMinTrackSize.y = 360;
            return 0;
        }
        case WM_DPICHANGED: {
            // Use the suggested rectangle (MS guidance) to reposition/resize at the new DPI. :contentReference[oaicite:6]{index=6}
            const RECT* suggested = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            // Update cached DPI and rebuild HUD font at the new DPI.
            [[maybe_unused]] UINT dpiX = LOWORD(w); // fix C4189: mark intentionally unused
            UINT dpiY = HIWORD(w);
            dpi_ = (dpiY != 0) ? dpiY : dpi_;
            if (font_) { DeleteObject(font_); font_ = nullptr; }
            LOGFONTW lf{}; lf.lfHeight = -MulDiv(10, (int)dpi_, 96);
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            font_ = CreateFontIndirectW(&lf);
            return 0;
        }
        case WM_SIZE: {
            clientW_ = LOWORD(l); clientH_ = HIWORD(l);
            HDC hdc = GetDC(h);
            if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);
            ReleaseDC(h, hdc);
            return 0;
        }
        case WM_ERASEBKGND:
            // We fully repaint from the back buffer; tell GDI no extra erase needed (reduces flicker).
            return 1;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT && rawPanActive_) { SetCursor(LoadCursor(nullptr, IDC_SIZEALL)); return TRUE; }
            break;
        case WM_MBUTTONDOWN: { // raw-pan while MMB is held
            rawPanActive_ = true; SetCapture(h);
            return 0;
        }
        case WM_MBUTTONUP: {
            rawPanActive_ = false; ReleaseCapture();
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
            // Respect WHEEL_DELTA=120 and scale per detent (handles high-res wheels). :contentReference[oaicite:7]{index=7}
            const int detents = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
            if (detents != 0) {
                zoom_ = util::clamp(zoom_ * std::pow(1.1, detents), 0.5, 2.5);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            switch (w) {
                case VK_ESCAPE:
                    if (buildMode_) { buildMode_=false; selected_.reset(); }
                    else { running_=false; }
                    break;
                case 'P': paused_ = !paused_; break;
                case VK_F11: ToggleBorderless(); break; // F11 also toggles borderless
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
        case WM_INPUT: { // raw input panning
            if (!cfg_.rawInput || !rawPanActive_) return 0;
            UINT sz = 0;
            GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
            if (sz == 0) return 0;
            std::vector<BYTE> buf(sz);
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf.data(), &sz, sizeof(RAWINPUTHEADER)) != sz) return 0;
            RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buf.data());
            if (ri->header.dwType == RIM_TYPEMOUSE) {
                LONG dx = ri->data.mouse.lLastX;
                LONG dy = ri->data.mouse.lLastY;
                // Drag world with the mouse; scale by 1/zoom so speed feels consistent.
                double k = 1.0 / std::max(0.1, zoom_);
                camera_.x -= dx * k;
                camera_.y -= dy * k;
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

    // ------------------ Gamepad (XInput) ------------------
    void InitGamepad()
    {
        LoadXInput();
        if (!g_XInputGetState) { padConnected_ = false; return; }
        for (DWORD i = 0; i < 4; ++i) {
            XINPUT_STATE st{};
            if (g_XInputGetState(i, &st) == ERROR_SUCCESS) {
                padIndex_ = i; padConnected_ = true; padPrev_ = st; break;
            }
        }
    }
    void SetRumble(float seconds, WORD left = 30000, WORD right = 0)
    {
        if (!padConnected_ || !g_XInputSetState) return;
        XINPUT_VIBRATION vib{ left, right };
        g_XInputSetState(padIndex_, &vib);
        rumbleUntil_ = seconds;
    }
    bool WasPressed(const XINPUT_STATE& now, WORD buttonMask)
    {
        const WORD was = padPrev_.Gamepad.wButtons & buttonMask;
        const WORD is  = now.Gamepad.wButtons & buttonMask;
        return !was && is;
    }
    void PollGamepad(double dt)
    {
        if (!g_XInputGetState) return;

        XINPUT_STATE st{};
        if (g_XInputGetState(padIndex_, &st) != ERROR_SUCCESS) {
            padConnected_ = false;
            for (DWORD i = 0; i < 4; ++i) {
                if (g_XInputGetState(i, &st) == ERROR_SUCCESS) { padIndex_ = i; padConnected_ = true; break; }
            }
            if (!padConnected_) return;
        }

        // Left stick pan (with dead-zone); Y inverted for screen space
        const float lx = NormalizeThumb(st.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const float ly = NormalizeThumb(st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        padPanX_ = lx;
        padPanY_ = -ly;

        // Triggers zoom
        const bool lt = st.Gamepad.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        const bool rt = st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        if (lt || rt) {
            double z = zoom_;
            if (rt) z = util::clamp(z * (1.0 + 0.75 * dt), 0.5, 2.5);
            if (lt) z = util::clamp(z * (1.0 - 0.75 * dt), 0.5, 2.5);
            zoom_ = z;
        }

        // A place (if in build mode), B cancel, X bulldoze, Y spawn colonist
        if (WasPressed(st, XINPUT_GAMEPAD_A) && buildMode_ && selected_.has_value()) {
            Vec2i t = MouseToTile(lastMouse_);
            if (TryQueueBuild(*selected_, t)) {
                SetRumble(0.15f, 25000, 0);
                buildMode_ = false; selected_.reset();
            } else {
                SetRumble(0.10f, 12000, 0);
            }
        }
        if (WasPressed(st, XINPUT_GAMEPAD_B) && buildMode_) {
            buildMode_ = false; selected_.reset();
        }
        if (WasPressed(st, XINPUT_GAMEPAD_X)) {
            auto t = MouseToTile(lastMouse_);
            Bulldoze(t);
            SetRumble(0.08f, 18000, 0);
        }
        if (WasPressed(st, XINPUT_GAMEPAD_Y)) {
            SpawnColonist();
            SetRumble(0.08f, 20000, 0);
        }

        // LB/RB quick-select
        if (WasPressed(st, XINPUT_GAMEPAD_LEFT_SHOULDER))  { selected_ = BuildingKind::Solar;  buildMode_ = true; }
        if (WasPressed(st, XINPUT_GAMEPAD_RIGHT_SHOULDER)) { selected_ = BuildingKind::Habitat;buildMode_ = true; }

        // DPad adjust sim speed; Start pause
        if (WasPressed(st, XINPUT_GAMEPAD_DPAD_UP))   simSpeed_ = util::clamp(simSpeed_ * 1.25, 0.25, 8.0);
        if (WasPressed(st, XINPUT_GAMEPAD_DPAD_DOWN)) simSpeed_ = util::clamp(simSpeed_ / 1.25, 0.25, 8.0);
        if (WasPressed(st, XINPUT_GAMEPAD_START))     paused_ = !paused_;

        // Rumble timeout
        if (rumbleUntil_ > 0.0) {
            rumbleUntil_ -= dt;
            if (rumbleUntil_ <= 0.0 && g_XInputSetState) {
                XINPUT_VIBRATION vib{ 0, 0 };
                g_XInputSetState(padIndex_, &vib);
            }
        }

        padPrev_ = st;
    }

    // ------------------ World / Sim init ------------------
    void InitWorld() {
        tileSize_ = 24;
        world_.resize(120, 80);
        world_.generate(rng_);

        hq_ = { world_.W/2, world_.H/2 };
        TryPlaceImmediate(BuildingKind::Solar,  hq_ + Vec2i{3,-2});
        TryPlaceImmediate(BuildingKind::Habitat,hq_ + Vec2i{3, 0});
        TryPlaceImmediate(BuildingKind::OxyGen, hq_ + Vec2i{0, 3});

        camera_.x = (hq_.x*tileSize_) - clientW_/2;
        camera_.y = (hq_.y*tileSize_) - clientH_/2;

        SpawnColonist();
    }

    void SpawnColonist() {
        Colonist c; c.id = nextColonistId_++; c.tile = hq_; colonists_.push_back(c);
        Banner(L"Colonist arrived");
    }

    // ------------------ Input helpers ------------------
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

    // ------------------ Build placement ------------------
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

    // ------------------ Update loop ------------------
    void Update(double dt) {
        // Camera pan (keyboard)
        const double pan=300.0;
        camera_.x += keyPan_.x * pan * dt;
        camera_.y += keyPan_.y * pan * dt;
        // Camera pan (gamepad analog)
        camera_.x += padPanX_ * pan * dt;
        camera_.y += padPanY_ * pan * dt;

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
            if (b.def.needsDaylight && !daylight) { /* no production */ }
            else colony_.powerBalance += b.def.powerProd;
            colony_.powerBalance -= b.def.powerCons;

            colony_.oxygenBalance += b.def.oxyProd;
            colony_.oxygenBalance -= b.def.oxyCons;
            colony_.waterBalance  += b.def.waterProd;
            colony_.waterBalance  -= b.def.waterCons;
            colony_.housing       += b.def.housing;
        }
        colony_.store.oxygen = std::max(0, colony_.store.oxygen + colony_.oxygenBalance);
        colony_.store.water  = std::max(0, colony_.store.water + colony_.waterBalance);
        int people = (int)colonists_.size();
        if (people>0) {
            colony_.store.oxygen = std::max(0, colony_.store.oxygen - people);
            colony_.store.water  = std::max(0, colony_.store.water  - people);
        }
        colony_.population=people;

        // If pending build and enough resources, "assign" when a colonist arrives
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
            std::deque<Vec2i> path; if(findPath(world_, c.tile, best, path)) {
                c.path=std::move(path); c.state=Colonist::State::Moving;
                c.job={ (tt==TileType::Ice)?JobType::MineIce:JobType::MineRock, best, 18, 0, 0 };
                return true;
            }
        }
        return false;
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
                if (findPath(world_, c.tile, pick, path)) {
                    c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Build,pendingBuild_->pos,18,0,pendingBuild_->id};
                    return;
                }
            }
        }
        if (colony_.store.oxygen < 40) if (TryAssignMining(c, TileType::Ice)) return;
        if (TryAssignMining(c, TileType::Rock)) return;
        // wander to HQ
        if (c.tile != hq_) {
            std::deque<Vec2i> path; if(findPath(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,0,0};}
        }
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
            std::deque<Vec2i> path; if(findPath(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,mined,0};}
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

    // ------------------ Rendering ------------------
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
            // grid line (subtle)
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
            HBRUSH br=CreateSolidBrush(RGB(255,255,255)); // translucent-ish: simulate with hatch
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
        int pad=8, w=540, h=110;
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
        DrawTextLine(x,y, L"1=Solar  2=Hab  3=O2Gen   LMB place  RMB cancel  MMB drag=pan(raw)  G colonist  P pause  +/- speed  Arrows pan  F11/Alt+Enter borderless");

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
    UINT dpi_ = 96;

    // Camera
    struct { double x=0,y=0; } camera_;
    double zoom_=1.0;

    // Config/paths
    AppPaths paths_;
    Config   cfg_;

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
    bool rawPanActive_ = false; // true while MMB held down

    // Window mode toggling
    DWORD origStyle_ = 0, origExStyle_ = 0;
    RECT  windowedRect_{};
    bool  borderless_ = false;

    // Gamepad state
    bool   padConnected_ = false;
    DWORD  padIndex_     = 0;
    XINPUT_STATE padPrev_{};
    double rumbleUntil_  = 0.0;
    double padPanX_      = 0.0;
    double padPanY_      = 0.0;

    // Banner
    std::wstring banner_; double bannerTime_=0.0;

    // ---- Borderless/fullscreen helpers ----
    RECT MonitorRect() const {
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(mon, &mi)) {
            RECT r{}; GetWindowRect(hwnd_, &r); return r;
        }
        return mi.rcMonitor;
    }
    void ApplyWindowMode(bool borderless) {
        borderless_ = borderless;
        DWORD style  = (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE);
        DWORD exstyle= (DWORD)GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if (!origStyle_)  origStyle_  = style;
        if (!origExStyle_)origExStyle_= exstyle;

        if (borderless_) {
            // Remember the current windowed rect so we can restore it later.
            GetWindowRect(hwnd_, &windowedRect_);
            SetWindowLongPtrW(hwnd_, GWL_STYLE,  (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE,(exstyle & ~(WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE)));
            RECT mr = MonitorRect();
            SetWindowPos(hwnd_, HWND_TOP, mr.left, mr.top,
                         mr.right - mr.left, mr.bottom - mr.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            // Restore styles and last windowed placement (or center with default size).
            SetWindowLongPtrW(hwnd_, GWL_STYLE,  (origStyle_ & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, origExStyle_);

            RECT wr = windowedRect_;
            if (wr.right <= wr.left || wr.bottom <= wr.top) {
                // Fall back to config size centered on current monitor.
                RECT mr = MonitorRect();
                int W = (int)cfg_.width, H = (int)cfg_.height;
                wr.left = mr.left + ((mr.right - mr.left) - W)/2;
                wr.top  = mr.top  + ((mr.bottom - mr.top) - H)/2;
                wr.right = wr.left + W; wr.bottom = wr.top + H;
            }
            // Ensure the window frame is applied.
            SetWindowPos(hwnd_, nullptr, wr.left, wr.top,
                         wr.right - wr.left, wr.bottom - wr.top,
                         SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
    void ToggleBorderless() { ApplyWindowMode(!borderless_); }
};

