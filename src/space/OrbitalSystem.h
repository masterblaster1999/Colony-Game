#pragma once
// OrbitalSystem.h - minimal, self-contained Keplerian system for star/planets/moons.
// Uses double-precision for orbits, converts to float for rendering.

#include <vector>
#include <string>
#include <random>
#include <cstdint>
#include <optional>
#include <DirectXMath.h>

namespace colony::space {

using namespace DirectX;

constexpr double PI = 3.1415926535897932384626433832795;
constexpr double TWO_PI = 2.0 * PI;

// Astronomical constants (km, s)
constexpr double AU_KM   = 149'597'870.7;
constexpr double MU_SUN  = 1.32712440018e11; // km^3 / s^2  (G * M_sun)
constexpr double DAY_S   = 86400.0;

// Scene scale (you can tweak these for your renderer)
constexpr double AU_TO_UNITS = 50.0;     // 1 AU = 50 scene units
constexpr double KM_TO_UNITS = AU_TO_UNITS / AU_KM;
constexpr double PLANET_RADIUS_SCALE = 6000.0; // exaggerate radii for visibility

enum class BodyType : uint8_t { Star, Planet, Moon };

struct Vec3d {
    double x{}, y{}, z{};
};

struct Color {
    float r{1}, g{1}, b{1}, a{1};
};

// Classical orbital elements (heliocentric or relative to parent)
struct OrbitalElements {
    // Angles in radians
    double semiMajorAxisAU = 1.0;  // a
    double eccentricity    = 0.01; // e (0..1)
    double inclination     = 0.0;  // i
    double longAscNode     = 0.0;  // Ω
    double argPeriapsis    = 0.0;  // ω
    double meanAnomAtEpoch = 0.0;  // M0 (at epochSeconds)

    // Derived
    double periodDays      = 365.25; // from Kepler’s third law
};

struct Body {
    std::string name;
    BodyType    type = BodyType::Planet;
    int         parentIndex = -1;         // -1 for star
    double      massSolar   = 0.0;        // for star; optional for planets
    double      radiusKm    = 1000.0;     // visual only
    Color       color       = {0.7f,0.7f,0.7f,1.0f};
    OrbitalElements elem{};               // ignored for star
    Vec3d       worldPosKm{};             // updated each tick
};

struct SystemConfig {
    uint64_t seed = 0xC01ony;
    int minPlanets = 4;
    int maxPlanets = 9;
    bool generateMoons = true;
    int maxMoonsPerPlanet = 2;
};

struct VisualScale {
    double auToUnits      = AU_TO_UNITS;
    double kmToUnits      = KM_TO_UNITS;
    double radiusScale    = PLANET_RADIUS_SCALE;
};

// Entire system
class OrbitalSystem {
public:
    static OrbitalSystem Generate(const SystemConfig& cfg);

    // Advance simulation time; absoluteTimeDays is “game clock” (days) since arbitrary epoch
    void Update(double absoluteTimeDays);

    const std::vector<Body>& Bodies() const noexcept { return m_bodies; }
    const VisualScale& Scale() const noexcept { return m_scale; }
    void SetScale(const VisualScale& s) { m_scale = s; }

    // Precomputed orbit line points (local to parent frame, in scene units)
    struct OrbitLine {
        int bodyIndex = -1;            // which body this orbit belongs to (not star)
        int parentIndex = -1;          // for transform (planet: -1/star, moon: planet idx)
        std::vector<DirectX::XMFLOAT3> points; // closed line strip (last==first)
        Color color{1,1,1,0.4f};
    };
    const std::vector<OrbitLine>& OrbitLines() const noexcept { return m_orbitLines; }

private:
    // Internal
    static double Clamp(double v, double a, double b) { return v<a ? a : (v>b ? b : v); }
    static double Frand(std::mt19937_64& rng, double lo, double hi);
    static int    Irand(std::mt19937_64& rng, int lo, int hi);
    static double DegToRad(double d) { return d * (PI/180.0); }

    static double KeplerPeriodDays(double aAU, double mu = MU_SUN);
    static double SolveKeplerE(double M, double e); // E from M,e
    static Vec3d  StateVectorKm(const OrbitalElements& el, double tDays);

    void BuildOrbitLines();

private:
    std::vector<Body> m_bodies; // [0] = star
    std::vector<OrbitLine> m_orbitLines;
    VisualScale m_scale{};
    double m_epochDays = 0.0;        // epoch for M0
    double m_timeDays  = 0.0;        // current absolute time (days)
};

} // namespace colony::space
