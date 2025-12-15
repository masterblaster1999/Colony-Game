// platform/win/LauncherEmbeddedSafeModeWin.h
#pragma once

#include <ostream>

#ifdef COLONY_EMBED_GAME_LOOP
namespace winlaunch
{
    int RunEmbeddedGameLoop(std::wostream& log);
}
#endif // COLONY_EMBED_GAME_LOOP
