#include "ui/Hud.h"
#include "sim/World.h"
#include "sim/Economy.h"
#include "ui/Camera.h"

// If your HUD touched SDL directly, include it here (not in Hud.h)
#include <SDL.h>

// === MOVE FROM GAME.CPP: exact HUD drawing code ===
//
// void hudRender(SDL_Renderer* r, const World& w, const Colony& c, const Camera& cam){...}
