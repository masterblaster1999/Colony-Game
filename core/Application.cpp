// core/Application.cpp
#include "core/Application.hpp"
#include "core/Game.hpp"
#include "core/Window.hpp" // wraps HWND + message pump

int RunColonyGame(HINSTANCE hInstance)
{
    Window window{hInstance, L"Colony Game", 1600, 900};
    Game   game;

    while (!window.shouldClose())
    {
        window.pollMessages();
        game.tick();   // update simulation, AI, jobs
        game.render(); // call renderer
    }
    return 0;
}
