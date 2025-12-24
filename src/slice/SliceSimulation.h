// src/slice/SliceSimulation.h
#pragma once

/*
    SliceSimulation
    --------------
    Vertical-slice simulation + gameplay state extracted from the original
    monolithic VerticalSlice.cpp.

    This module owns:
      - Cameras + input toggles (polling via Win32 GetAsyncKeyState)
      - OrbitalSystem state + selection/follow logic
      - ObjectiveTracker glue (debug hotkeys + survival timer)
      - CPU-side FPS counter

    It does NOT own D3D11 resources (see SliceRendererD3D11).
*/

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <DirectXMath.h>

#include <array>
#include <cstdint>

#include "space/OrbitalSystem.h"
#include "render/OrbitalRenderer.h" // OrbitalRendererOptions
#include "slice/ObjectiveTracker.h"  // ObjectiveTracker + SliceState

namespace slice {

// Global objective tracker used by the vertical slice (defined in SliceSimulation.cpp)
extern ObjectiveTracker g_slice;

struct FPSCounter {
    double acc = 0.0;
    int frames = 0;
    double fps = 0.0;
    double ms = 0.0;

    void tick(double dt);
};

struct OrbitCam {
    DirectX::XMFLOAT3 target{0, 0, 0};
    float radius = 18.f;
    float yawDeg = 35.f;
    float pitchDeg = 25.f;

    DirectX::XMMATRIX view() const;
};

struct FreeCam {
    DirectX::XMVECTOR pos = DirectX::XMVectorSet(0, 3, -8, 0);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 8.0f;
    float mouseSens = 0.0025f;

    void processMouse(float dx, float dy);
    void processKeys(float dt);
    DirectX::XMMATRIX view() const;
};

class SliceSimulation {
public:
    enum class CamMode { Orbit = 0, Free = 1 };

    // --- Simulation state ---
    uint32_t seed = 1337;

    // Orbital
    colony::space::OrbitalSystem orbital;
    colony::space::OrbitalRendererOptions orbOpts{};

    // Cameras
    CamMode camMode = CamMode::Orbit;
    OrbitCam orbitCam{};
    FreeCam freeCam{};
    bool rightMouseWasDown = false;
    POINT lastMouse{};
    float fovDeg = 60.f;

    // Selection/follow
    int selectedBody = 0;
    bool followSelected = false;

    // Controls / sim
    bool vsync = true;
    bool paused = false;
    bool singleStep = false;
    bool drawCube = true;
    bool orbitBlend = true;
    bool wireframe = false;

    double timeDays = 0.0;
    double timeScale = 5.0; // game days per real second

    float TileWorld = 0.5f;
    float HeightAmp = 6.0f;

    // Heightmap params (renderer consumes these)
    int HM = 128;
    float hmScale = 24.0f;
    int hmOctaves = 4;
    float hmPersistence = 0.5f;

    // Lighting
    DirectX::XMFLOAT3 lightDir = DirectX::XMFLOAT3(0.3f, 0.8f, 0.5f);

    // Perf
    FPSCounter fps{};

    // Requests for renderer-side actions (set during UpdateSim)
    bool requestRegenerateHeight = false;
    bool requestReloadOrbitalRenderer = false;
    bool requestScreenshot = false;

    // --- Lifecycle ---
    void initialize(uint32_t initialSeed);

    // Fixed-step simulation update.
    void updateSim(double dt);

    // Event handlers (called from the Win32 WndProc)
    void onMouseWheel(short delta);

    // Helpers for renderer/title
    DirectX::XMFLOAT3 bodyWorldUnits(int idx) const;

private:
    std::array<SHORT, 256> prevKey_{};

    bool keyPressed_(int vk);
    void selectNextBody_(int dir);

    void regenerateOrbital_(uint32_t newSeed);

    void handleInputToggles_();
    void updateCameraMouse_();
};

} // namespace slice
