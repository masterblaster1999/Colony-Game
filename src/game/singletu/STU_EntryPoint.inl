// ============================ Public Entry Point =============================

int RunColonyGame(const GameOptions& opts) {
    // Win boilerplate
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    Game game(hInst, opts);
    int rc = game.Run();

    CoUninitialize();
    return rc;
}

// ============================================================================
//                            EXPANSION POINTS
// ============================================================================
//
// You can paste more systems here without touching any other file.
//
// Suggested modules to reach/approach ~3,000 LOC:
//  1) Research & Tech Tree:
//      - Research building (consumes power/O2).
//      - Unlocks: Battery (stores power at day, releases at night), Water Extractor,
//        Refinery (regolith→metal), Greenhouse (water→food, grants morale bonus).
//      - UI panel: queue research, progress bars.
//
//  2) Vehicles & Logistics:
//      - Rover entity (faster hauling; pathfinding same API).
//      - Stockpile nodes & hauling tasks (priority queue).
//      - Roads (lower tile cost), buildable by colonists.
//
//  3) Events & Weather:
//      - Dust storms (reduce solar output, slow movement).
//      - Meteor strikes (spawn craters, damage buildings).
//      - Random “anomalies” that grant resources or tech points.
//
//  4) Colonist Simulation:
//      - Traits (Engineer/Scientist/Miner).
//      - Needs (morale, fatigue) that influence productivity.
//      - Homes/jobs assignment; vacancy checks against Habitat housing.
//
//  5) UI Panels:
//      - Build menu, Jobs panel, Resources panel, Messages log.
//      - Tooltips for buildings with production/consumption deltas.
//
//  6) Save/Load v2:
//      - Backward-compatible format; chunked sections with checksums.
//      - Autosave every N minutes.
//
//  7) Screenshot utility:
//      - BitBlt backbuffer to a BMP in %LOCALAPPDATA%\MarsColonySim\Screenshots.
//
// ============================================================================
