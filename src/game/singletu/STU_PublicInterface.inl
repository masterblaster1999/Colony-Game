// =============================== Public Interface ============================

struct GameOptions {
    int         width        = 1280;
    int         height       = 720;
    bool        fullscreen   = false;
    bool        vsync        = true;
    bool        safeMode     = false;
    uint64_t    seed         = 0;
    std::string profile      = "default";
    std::string lang         = "en-US";
    std::string saveDir;     // e.g. %LOCALAPPDATA%\MarsColonySim\Saves
    std::string assetsDir;   // e.g. .\assets
};

int RunColonyGame(const GameOptions& opts); // implemented at bottom

