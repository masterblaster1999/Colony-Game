#pragma once
#include <cstdint>
#include <string>

// Forward declare SDL types to avoid heavy includes here.
struct SDL_Renderer;
struct SDL_Window;

struct World;
struct Colony;
struct Camera;

// --- MOVE FROM GAME.CPP: HUD rendering (the text/overlay you already draw)
/*
void hudRender(SDL_Renderer* r, const World& w, const Colony& c, const Camera& cam);
*/
