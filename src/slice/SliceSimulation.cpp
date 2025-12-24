// src/slice/SliceSimulation.cpp

#include "SliceSimulation.h"

#include <algorithm>
#include <cmath>

namespace slice {

// -----------------------------------------------------------------------------
// Global objective tracker (single definition)
// -----------------------------------------------------------------------------
ObjectiveTracker g_slice =
    ObjectiveTracker::MakeDefault(/*surviveSeconds*/ 600.0,
                                 /*structuresToBuild*/ 2,
                                 /*itemsToCraft*/ 1,
                                 /*startingColonists*/ 3);

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------
static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static float ToRad(float deg) { return deg * (float)DirectX::XM_PI / 180.0f; }

// -----------------------------------------------------------------------------
// FPSCounter
// -----------------------------------------------------------------------------
void FPSCounter::tick(double dt) {
    acc += dt;
    frames++;
    if (acc >= 0.5) {
        fps = frames / acc;
        ms = 1000.0 * acc / frames;
        acc = 0.0;
        frames = 0;
    }
}

// -----------------------------------------------------------------------------
// Cameras
// -----------------------------------------------------------------------------
DirectX::XMMATRIX OrbitCam::view() const {
    const float cy = ToRad(yawDeg);
    const float cp = ToRad(pitchDeg);

    DirectX::XMVECTOR eyeOff = DirectX::XMVectorSet(
        radius * std::cos(cy) * std::cos(cp),
        radius * std::sin(cp),
        radius * std::sin(cy) * std::cos(cp),
        0.0f);

    DirectX::XMVECTOR tgt = DirectX::XMLoadFloat3(&target);
    return DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorAdd(tgt, eyeOff),
        tgt,
        DirectX::XMVectorSet(0, 1, 0, 0));
}

void FreeCam::processMouse(float dx, float dy) {
    yaw += dx * mouseSens;
    pitch += dy * mouseSens;

    const float k = ToRad(89.0f);
    if (pitch > k) pitch = k;
    if (pitch < -k) pitch = -k;
}

void FreeCam::processKeys(float dt) {
    const float speed = moveSpeed * dt * (KeyDown(VK_SHIFT) ? 3.0f : 1.0f);

    DirectX::XMVECTOR fwd = DirectX::XMVectorSet(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch),
        0.0f);

    DirectX::XMVECTOR right = DirectX::XMVector3Normalize(
        DirectX::XMVector3Cross(fwd, DirectX::XMVectorSet(0, 1, 0, 0)));
    DirectX::XMVECTOR up = DirectX::XMVector3Normalize(
        DirectX::XMVector3Cross(right, fwd));

    if (KeyDown('W')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(fwd, speed));
    if (KeyDown('S')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(fwd, -speed));
    if (KeyDown('A')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, -speed));
    if (KeyDown('D')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, speed));
    if (KeyDown('Q')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, -speed));
    if (KeyDown('E')) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, speed));
}

DirectX::XMMATRIX FreeCam::view() const {
    DirectX::XMVECTOR fwd = DirectX::XMVectorSet(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch),
        0.0f);

    return DirectX::XMMatrixLookToLH(pos, fwd, DirectX::XMVectorSet(0, 1, 0, 0));
}

// -----------------------------------------------------------------------------
// SliceSimulation
// -----------------------------------------------------------------------------

void SliceSimulation::initialize(uint32_t initialSeed) {
    seed = initialSeed;

    // Orbital system (same defaults as the original VerticalSlice.cpp)
    {
        colony::space::SystemConfig cfg{};
        cfg.seed = seed;
        cfg.minPlanets = 5;
        cfg.maxPlanets = 8;
        cfg.generateMoons = true;

        orbital = colony::space::OrbitalSystem::Generate(cfg);

        auto vs = orbital.Scale();
        vs.auToUnits = 6.0; // compact system for this slice
        vs.kmToUnits = vs.auToUnits / colony::space::AU_KM;
        vs.radiusScale = 7000.0;
        orbital.SetScale(vs);

        orbOpts.drawStar = true;
        orbOpts.drawPlanets = true;
        orbOpts.drawMoons = true;
        orbOpts.drawOrbits = true;
        orbOpts.sphereSubdiv = 2;

        selectedBody = 0;
        followSelected = false;
    }

    GetCursorPos(&lastMouse);

    // Localization for the default tracker tokens (same mapping as before)
    g_slice.setLocalizer([](std::string_view tok) -> std::string {
        if (tok == "EstablishColony") return "Establish the colony";
        if (tok == "BuildDesc") return "Build structures";
        if (tok == "BuildStructures") return "Build structures";
        if (tok == "EnableProduction") return "Enable production";
        if (tok == "CraftDesc") return "Craft items";
        if (tok == "CraftItems") return "Craft items";
        if (tok == "WeatherTheNight") return "Weather the night";
        if (tok == "SurviveDesc") return "Survive the timer";
        if (tok == "SurviveTimer") return "Survive timer";
        if (tok == "NoDeaths60s") return "No deaths in last 60s";
        if (tok == "NoRecentDeaths") return "No recent deaths";
        if (tok == "KeepThemAlive") return "Keep them alive";
        if (tok == "EndWith3Colonists") return "Finish with at least 3 colonists alive";
        if (tok == "ColonistsGte3") return "Colonists \u2265 3";
        return std::string(tok);
    });
}

bool SliceSimulation::keyPressed_(int vk) {
    const SHORT cur = GetAsyncKeyState(vk);
    const bool wasDown = (prevKey_[vk] & 0x8000) != 0;
    const bool isDown = (cur & 0x8000) != 0;
    prevKey_[vk] = cur;
    return isDown && !wasDown;
}

DirectX::XMFLOAT3 SliceSimulation::bodyWorldUnits(int idx) const {
    const auto& b = orbital.Bodies()[static_cast<size_t>(idx)];
    const auto& s = orbital.Scale();
    return DirectX::XMFLOAT3(
        float(b.worldPosKm.x * s.kmToUnits),
        float(b.worldPosKm.y * s.kmToUnits),
        float(b.worldPosKm.z * s.kmToUnits));
}

void SliceSimulation::selectNextBody_(int dir) {
    if (orbital.Bodies().empty()) return;

    const int count = static_cast<int>(orbital.Bodies().size());
    selectedBody = (selectedBody + dir + count) % count;

    if (followSelected) {
        orbitCam.target = bodyWorldUnits(selectedBody);
    }
}

void SliceSimulation::regenerateOrbital_(uint32_t newSeed) {
    colony::space::SystemConfig cfg{};
    cfg.seed = newSeed;
    cfg.minPlanets = 4 + (newSeed % 6); // 4..9
    cfg.maxPlanets = std::max(cfg.minPlanets, 9);
    cfg.generateMoons = true;

    orbital = colony::space::OrbitalSystem::Generate(cfg);

    auto vs = orbital.Scale();
    vs.auToUnits = 6.0;
    vs.kmToUnits = vs.auToUnits / colony::space::AU_KM;
    vs.radiusScale = 7000.0;
    orbital.SetScale(vs);

    if (orbital.Bodies().empty()) {
        selectedBody = 0;
    } else {
        const int maxIdx = static_cast<int>(orbital.Bodies().size()) - 1;
        if (selectedBody > maxIdx) selectedBody = maxIdx;
        if (selectedBody < 0) selectedBody = 0;
    }
}

void SliceSimulation::handleInputToggles_() {
    if (keyPressed_(VK_F1)) { wireframe = !wireframe; }
    if (keyPressed_('V')) { vsync = !vsync; }
    if (keyPressed_(VK_SPACE)) { paused = !paused; }
    if (keyPressed_('G')) { singleStep = true; } // step one fixed frame when paused

    if (keyPressed_(VK_OEM_PLUS) || keyPressed_(VK_ADD)) { timeScale *= 1.25; }
    if (keyPressed_(VK_OEM_MINUS) || keyPressed_(VK_SUBTRACT)) { timeScale = std::max(0.01, timeScale / 1.25); }

    if (keyPressed_('R')) { regenerateOrbital_(++seed); }
    if (keyPressed_('N')) { requestRegenerateHeight = true; }

    if (keyPressed_('O')) { orbOpts.drawOrbits = !orbOpts.drawOrbits; }
    if (keyPressed_('P')) { orbOpts.drawPlanets = !orbOpts.drawPlanets; }
    if (keyPressed_('M')) { orbOpts.drawMoons = !orbOpts.drawMoons; }
    if (keyPressed_('T')) { orbOpts.drawStar = !orbOpts.drawStar; }

    if (keyPressed_('B')) { orbitBlend = !orbitBlend; }
    if (keyPressed_('H')) { drawCube = !drawCube; }

    if (keyPressed_('1')) { camMode = CamMode::Orbit; }
    if (keyPressed_('2')) { camMode = CamMode::Free; }

    // Orbital renderer hot-reload (shaders/buffers)
    if (keyPressed_('F')) { requestReloadOrbitalRenderer = true; }

    if (keyPressed_(VK_OEM_4)) { // '[' lower height amplitude
        HeightAmp = std::max(0.1f, HeightAmp - 0.5f);
    }
    if (keyPressed_(VK_OEM_6)) { // ']' raise height amplitude
        HeightAmp += 0.5f;
    }

    if (keyPressed_(VK_F12)) { requestScreenshot = true; }

    if (keyPressed_(VK_OEM_COMMA)) { selectNextBody_(-1); }   // ',' prev
    if (keyPressed_(VK_OEM_PERIOD)) { selectNextBody_(+1); }  // '.' next

    if (keyPressed_('L')) {
        followSelected = !followSelected;
        if (followSelected) {
            orbitCam.target = bodyWorldUnits(selectedBody);
        }
    }

    if (keyPressed_('C')) { // reset target
        followSelected = false;
        orbitCam.target = DirectX::XMFLOAT3(0, 0, 0);
    }

    if (keyPressed_('3')) { fovDeg = std::max(20.0f, fovDeg - 2.0f); }
    if (keyPressed_('4')) { fovDeg = std::min(120.0f, fovDeg + 2.0f); }

    // Objective tracker debug events (simulate slice loop)
    if (keyPressed_('Y')) { g_slice.notifyStructureBuilt(); }
    if (keyPressed_('U')) { g_slice.notifyItemCrafted(); }
    if (keyPressed_('J')) { g_slice.notifyColonistSpawned(); }
    if (keyPressed_('K')) { g_slice.notifyColonistDied(); }
}

void SliceSimulation::updateCameraMouse_() {
    const bool rmb = KeyDown(VK_RBUTTON);

    POINT p{};
    GetCursorPos(&p);

    if (rmb && rightMouseWasDown) {
        const float dx = float(p.x - lastMouse.x);
        const float dy = float(p.y - lastMouse.y);

        if (camMode == CamMode::Orbit) {
            orbitCam.yawDeg += dx * 0.25f;
            orbitCam.pitchDeg -= dy * 0.25f;

            if (orbitCam.pitchDeg < -89.0f) orbitCam.pitchDeg = -89.0f;
            if (orbitCam.pitchDeg > 89.0f) orbitCam.pitchDeg = 89.0f;
        } else {
            // Original behavior: dy inverted before passing to FreeCam
            freeCam.processMouse(dx, -dy);
        }
    }

    rightMouseWasDown = rmb;
    lastMouse = p;

    // Orbit cam radius with PgUp/PgDn
    if (camMode == CamMode::Orbit) {
        if (KeyDown(VK_NEXT)) orbitCam.radius = std::min(100.0f, orbitCam.radius + 0.5f);
        if (KeyDown(VK_PRIOR)) orbitCam.radius = std::max(2.0f, orbitCam.radius - 0.5f);
    }
}

void SliceSimulation::updateSim(double dt) {
    handleInputToggles_();
    updateCameraMouse_();

    if (camMode == CamMode::Free) {
        freeCam.processKeys((float)dt);
    }

    // Light animation (slow rotate)
    static float tLight = 0.0f;
    tLight += (float)dt * 0.2f;
    lightDir = DirectX::XMFLOAT3(std::cos(tLight) * 0.3f, 0.8f, std::sin(tLight) * 0.5f);

    // Feed objective tracker time
    if (!paused) {
        g_slice.update(dt);
    } else if (singleStep) {
        g_slice.update(dt); // single-step advances once while paused
    }

    if (!paused || singleStep) {
        timeDays += dt * timeScale;
        singleStep = false;
    }

    orbital.Update(timeDays);

    // Follow selected body
    if (followSelected && selectedBody >= 0 && selectedBody < (int)orbital.Bodies().size()) {
        orbitCam.target = bodyWorldUnits(selectedBody);
    }
}

void SliceSimulation::onMouseWheel(short delta) {
    if (camMode != CamMode::Orbit) return;

    if (delta > 0) orbitCam.radius = std::max(2.0f, orbitCam.radius - 1.0f);
    else orbitCam.radius = std::min(100.0f, orbitCam.radius + 1.0f);
}

} // namespace slice
