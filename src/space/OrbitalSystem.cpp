// OrbitalSystem.cpp
#include "OrbitalSystem.h"
#include <algorithm>
#include <cmath>
#include <cassert>

using namespace DirectX;

namespace colony::space {

double OrbitalSystem::Frand(std::mt19937_64& rng, double lo, double hi) {
    std::uniform_real_distribution<double> d(lo, hi);
    return d(rng);
}
int OrbitalSystem::Irand(std::mt19937_64& rng, int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
}

// Kepler’s third law (elliptical): P = 2π sqrt(a^3 / μ)
// a in AU; convert to km for μ in km^3/s^2; return days.
double OrbitalSystem::KeplerPeriodDays(double aAU, double mu) {
    double aKm = aAU * AU_KM;
    double P_s = TWO_PI * std::sqrt((aKm*aKm*aKm) / mu);
    return P_s / DAY_S;
}

// Solve E - e sin E = M for E (elliptical), Newton-Raphson
double OrbitalSystem::SolveKeplerE(double M, double e) {
    M = std::fmod(M, TWO_PI);
    if (M < 0) M += TWO_PI;
    double E = (e < 0.8) ? M : PI; // good initial guesses
    for (int i=0; i<16; ++i) {
        double f  = E - e*std::sin(E) - M;
        double fp = 1.0 - e*std::cos(E);
        double dE = -f / fp;
        E += dE;
        if (std::abs(dE) < 1e-12) break;
    }
    return E;
}

// Compute heliocentric (or parent-centric) position in km at time tDays
// Using classical elements and rotation: Rz(Ω) * Rx(i) * Rz(ω) * r_orb
Vec3d OrbitalSystem::StateVectorKm(const OrbitalElements& el, double tDays) {
    double n = TWO_PI / el.periodDays; // mean motion (rad/day)
    double M = el.meanAnomAtEpoch + n * tDays;
    double E = SolveKeplerE(M, el.eccentricity);

    double a = el.semiMajorAxisAU * AU_KM;
    double b = a * std::sqrt(1.0 - el.eccentricity*el.eccentricity);

    double cosE = std::cos(E);
    double sinE = std::sin(E);

    double x_orb = a * (cosE - el.eccentricity);
    double y_orb = b * (sinE);

    double cosO = std::cos(el.longAscNode);
    double sinO = std::sin(el.longAscNode);
    double cosi = std::cos(el.inclination);
    double sini = std::sin(el.inclination);
    double cosw = std::cos(el.argPeriapsis);
    double sinw = std::sin(el.argPeriapsis);

    // Precompute rotation terms (see standard orbital transform)
    double R11 =  cosO*cosw - sinO*sinw*cosi;
    double R12 = -cosO*sinw - sinO*cosw*cosi;
    double R21 =  sinO*cosw + cosO*sinw*cosi;
    double R22 = -sinO*sinw + cosO*cosw*cosi;
    double R31 =  sinw*sini;
    double R32 =  cosw*sini;

    Vec3d r{};
    r.x = R11 * x_orb + R12 * y_orb;
    r.y = R21 * x_orb + R22 * y_orb;
    r.z = R31 * x_orb + R32 * y_orb;
    return r;
}

// ---------------- Generation ----------------

OrbitalSystem OrbitalSystem::Generate(const SystemConfig& cfg) {
    OrbitalSystem sys;
    std::mt19937_64 rng(cfg.seed);

    // --- Star ---
    Body star{};
    star.type = BodyType::Star;
    star.parentIndex = -1;
    star.name = "Alpha";
    star.massSolar = Frand(rng, 0.7, 1.3);
    star.radiusKm = 696'340.0 * star.massSolar; // rough: scale radius ~ mass
    star.color = {1.0f, 0.95f, 0.85f, 1.0f};    // warm white
    star.worldPosKm = {0,0,0};
    sys.m_bodies.push_back(star);

    // --- Planets ---
    int nPlanets = Irand(rng, cfg.minPlanets, cfg.maxPlanets);
    double a0 = Frand(rng, 0.30, 0.45);       // inner-most a (AU)
    double spacing = Frand(rng, 1.4, 1.9);    // Titius-Bode-like spacing

    for (int i=0; i<nPlanets; ++i) {
        Body p{};
        p.type = BodyType::Planet;
        p.parentIndex = 0; // star
        p.name = "Alpha-" + std::to_string(i+1);

        // Semi-major axis
        double aAU = a0 * std::pow(spacing, i) * Frand(rng, 0.95, 1.05);

        // Eccentricities small-ish, more for outer planets
        double e = std::clamp(Frand(rng, 0.0, 0.2) + 0.02*std::sqrt((double)i), 0.0, 0.35);

        // Slight inclinations (degrees -> rad)
        double inc = DegToRad(Frand(rng, 0.0, 5.0) * (i+1) / (double)std::max(1,i));

        // Angles uniform
        double omega = Frand(rng, 0.0, TWO_PI);
        double bigOmega = Frand(rng, 0.0, TWO_PI);
        double M0 = Frand(rng, 0.0, TWO_PI);

        // Period from Kepler, using star mass
        double mu = MU_SUN * sys.m_bodies[0].massSolar;
        double P = KeplerPeriodDays(aAU, mu);

        // Visual radius: rocky vs gas giant by distance
        bool rocky = aAU < 2.2;
        if (rocky) {
            p.radiusKm = Frand(rng, 2500.0, 6500.0); // ~Mercury..Earth
            p.color = {0.7f, 0.65f, 0.58f, 1.0f};
        } else {
            p.radiusKm = Frand(rng, 20'000.0, 70'000.0); // ~Neptune..Jupiter
            // Blue-ish or beige-ish
            bool blue = Frand(rng, 0.0, 1.0) > 0.5;
            p.color = blue ? Color{0.55f,0.7f,0.9f,1.0f} : Color{0.9f,0.85f,0.7f,1.0f};
        }

        p.elem.semiMajorAxisAU = aAU;
        p.elem.eccentricity    = e;
        p.elem.inclination     = inc;
        p.elem.longAscNode     = bigOmega;
        p.elem.argPeriapsis    = omega;
        p.elem.meanAnomAtEpoch = M0;
        p.elem.periodDays      = P;

        sys.m_bodies.push_back(p);

        // --- Optional moons ---
        if (cfg.generateMoons && !rocky) {
            int nMoons = Irand(rng, 0, cfg.maxMoonsPerPlanet);
            for (int m=0; m<nMoons; ++m) {
                Body moon{};
                moon.type = BodyType::Moon;
                moon.parentIndex = (int)sys.m_bodies.size()-1; // parent is last inserted planet
                moon.name = p.name + "‑" + std::string(1, char('a'+m));

                // Place moons well within Hill sphere (very simplified)
                double aMoonKm = Frand(rng, 200'000.0, 1'500'000.0);
                double aMoonAU = aMoonKm / AU_KM;
                double eMoon   = Frand(rng, 0.0, 0.05);
                double incMoon = DegToRad(Frand(rng, 0.0, 5.0));
                double omegaM  = Frand(rng, 0.0, TWO_PI);
                double bigOM   = Frand(rng, 0.0, TWO_PI);
                double M0M     = Frand(rng, 0.0, TWO_PI);

                // Approximate μ using planet mass ~ scaled by radius (rough)
                double massPlanetVsJup = (p.radiusKm / 70'000.0); // hand-wavy
                double muPlanet = 1.26686534e8 * massPlanetVsJup; // km^3/s^2 (≈ μ_Jupiter scaled)

                double Pm = KeplerPeriodDays(aMoonAU, muPlanet > 1e-3 ? muPlanet : 1.0e8);

                moon.radiusKm = Frand(rng, 800.0, 3000.0);
                moon.color = {0.75f, 0.72f, 0.68f, 1.0f};

                moon.elem.semiMajorAxisAU = aMoonAU;
                moon.elem.eccentricity    = eMoon;
                moon.elem.inclination     = incMoon;
                moon.elem.longAscNode     = bigOM;
                moon.elem.argPeriapsis    = omegaM;
                moon.elem.meanAnomAtEpoch = M0M;
                moon.elem.periodDays      = Pm;

                sys.m_bodies.push_back(moon);
            }
        }
    }

    // Lines once at generation
    sys.BuildOrbitLines();

    // Initialize positions at epoch=0
    sys.m_epochDays = 0.0;
    sys.Update(0.0);
    return sys;
}

// Build orbit line vertices (local to each orbit’s parent), in scene units
void OrbitalSystem::BuildOrbitLines() {
    m_orbitLines.clear();
    const int steps = 256;
    for (int i=1; i<(int)m_bodies.size(); ++i) {
        const Body& b = m_bodies[i];
        OrbitLine line{};
        line.bodyIndex = i;
        line.parentIndex = b.parentIndex;
        line.color = (b.type == BodyType::Moon) ? Color{0.9f,0.9f,1.0f,0.35f}
                                                : Color{1.0f,1.0f,1.0f,0.25f};

        line.points.reserve(steps+1);

        // Sample the ellipse by E (eccentric anomaly) from 0..2π, transform by Ω,i,ω
        const auto& el = b.elem;

        double a = el.semiMajorAxisAU * AU_TO_UNITS; // in scene units directly
        double bAxis = a * std::sqrt(1.0 - el.eccentricity*el.eccentricity);

        double cosO = std::cos(el.longAscNode);
        double sinO = std::sin(el.longAscNode);
        double cosi = std::cos(el.inclination);
        double sini = std::sin(el.inclination);
        double cosw = std::cos(el.argPeriapsis);
        double sinw = std::sin(el.argPeriapsis);

        double R11 =  cosO*cosw - sinO*sinw*cosi;
        double R12 = -cosO*sinw - sinO*cosw*cosi;
        double R21 =  sinO*cosw + cosO*sinw*cosi;
        double R22 = -sinO*sinw + cosO*cosw*cosi;
        double R31 =  sinw*sini;
        double R32 =  cosw*sini;

        for (int s=0; s<=steps; ++s) {
            double t = (double)s / (double)steps;
            double E = t * TWO_PI;
            double cosE = std::cos(E);
            double sinE = std::sin(E);
            double x_orb = a * (cosE - el.eccentricity);
            double y_orb = bAxis * (sinE);
            double x = R11 * x_orb + R12 * y_orb;
            double y = R21 * x_orb + R22 * y_orb;
            double z = R31 * x_orb + R32 * y_orb;
            line.points.emplace_back((float)x, (float)y, (float)z);
        }
        m_orbitLines.push_back(std::move(line));
    }
}

// ---------------- Simulation Update ----------------

void OrbitalSystem::Update(double absoluteTimeDays) {
    m_timeDays = absoluteTimeDays;

    // Star fixed at origin
    if (!m_bodies.empty()) m_bodies[0].worldPosKm = {0,0,0};

    // Compute bodies in index order (parents appear before children)
    for (size_t i=1; i<m_bodies.size(); ++i) {
        Body& b = m_bodies[i];
        double tSinceEpoch = m_timeDays - m_epochDays;

        Vec3d rel = StateVectorKm(b.elem, tSinceEpoch);

        if (b.parentIndex >= 0) {
            const Body& parent = m_bodies[(size_t)b.parentIndex];
            b.worldPosKm = { parent.worldPosKm.x + rel.x,
                             parent.worldPosKm.y + rel.y,
                             parent.worldPosKm.z + rel.z };
        } else {
            b.worldPosKm = rel;
        }
    }
}

} // namespace colony::space
