#pragma once
#include <QObject>
#include <QString>
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Plain-old-data structs that carry live telemetry across the app.
// ──────────────────────────────────────────────────────────────────────────────

// Helper to calculate speed from power using physics
// totalMassKg = rider + bike (default 85 kg for a ~75 kg rider on a 10 kg bike)
// gradePct     = road grade in % (+ve = ascent, -ve = descent; default 0 = flat)
inline double calculateSpeedFromPower(double powerWatts,
                                      double totalMassKg = 85.0,
                                      double gradePct    = 0.0)
{
    // Grade model: P_eff = v × (m·g·Crr + m·g·grade/100 + 0.5·ρ·CdA·v²)
    // Positive grade → more resistance → lower speed.
    // Negative grade → gravity helps  → higher speed same power, and even when
    // coasting (power=0) gravity-driven terminal speed is computed correctly.

    // ~2.5% of crank power is lost to chain/gear friction before reaching the wheel
    const double effectivePower = (powerWatts > 0.0) ? powerWatts * 0.975 : 0.0;

    constexpr double crr = 0.004;         // good road tire on asphalt
    constexpr double cda = 0.36;          // typical road bike, hoods / moderate position
    constexpr double airDensity = 1.225;  // kg/m³ at sea level
    constexpr double g = 9.81;

    const double rollingForce = totalMassKg * g * crr;
    const double gradeForce   = totalMassKg * g * (gradePct / 100.0); // +ascent / -descent
    const double linearForce  = rollingForce + gradeForce;
    const double aeroCoeff    = 0.5 * airDensity * cda;

    // If net resistance force ≥ 0 and there is no power → can't move
    if (effectivePower <= 0.0 && linearForce >= 0.0) return 0.0;

    // Newton-Raphson solve for v: linearForce·v + aeroCoeff·v³ = effectivePower
    //
    // On a DESCENT (linearForce < 0) the cubic has a local max/min near v=0.
    // If we start below the turning-point (v_turn = sqrt(-lF/(3·aC))) NR steps
    // toward negative v and converges to 0 — the wrong root.
    // Fix: whenever linearForce < 0, start from the gravity-terminal-velocity
    // (sqrt(-lF/aC)) which is always above the turning point, so NR converges
    // to the correct physical root regardless of how much power is applied.
    double v;
    if (linearForce < 0.0) {
        v = std::sqrt(-linearForce / aeroCoeff); // above turning point → correct basin
    } else {
        v = std::cbrt(effectivePower / aeroCoeff); // flat/ascent: aero-dominant guess
    }
    if (v < 0.1) v = 0.1;
    for (int i = 0; i < 25; ++i) {
        const double f  = linearForce * v + aeroCoeff * v * v * v - effectivePower;
        const double df = linearForce + 3.0 * aeroCoeff * v * v;
        if (std::abs(df) <= 1e-9) break;
        const double dv = f / df;
        v -= dv;
        if (v < 0.0) { v = 0.0; break; }
        if (std::abs(dv) < 1e-9) break;
    }

    return v * 3.6; // m/s -> km/h
}
// Inertial speed model — one Euler step of the cycling equation of motion.
// Call once per power update; the state (currentSpeedMps) must be stored
// externally and threaded back on every call for smooth speed output.
//   currentSpeedMps : current speed in m/s (0 on first call)
//   powerWatts      : instantaneous power (W)
//   totalMassKg     : rider + bike mass (kg)
//   gradePct        : road grade in % (+ve = ascent)
//   dt              : time elapsed since last call (seconds)
// Returns the new speed in km/h.
inline double applySpeedInertia(double currentSpeedMps,
                                double powerWatts,
                                double totalMassKg = 85.0,
                                double gradePct    = 0.0,
                                double dt          = 1.0)
{
    constexpr double crr        = 0.004;
    constexpr double cda        = 0.36;
    constexpr double airDensity = 1.225;
    constexpr double g          = 9.81;
    constexpr double efficiency = 0.975;

    const double v = currentSpeedMps > 0.0 ? currentSpeedMps : 0.0;

    // Clamp to 0.5 m/s for force calculations to avoid ÷0 in fDrive
    const double vSafe  = v < 0.5 ? 0.5 : v;
    const double fDrive = powerWatts > 0.0 ? (powerWatts * efficiency) / vSafe : 0.0;
    const double fRoll  = totalMassKg * g * crr;
    const double fGrade = totalMassKg * g * (gradePct / 100.0);  // +ascent / -descent
    const double fAero  = 0.5 * airDensity * cda * v * v;

    const double fNet = fDrive - fRoll - fGrade - fAero;
    const double vNew = v + (fNet / totalMassKg) * dt;
    return (vNew > 0.0 ? vNew : 0.0) * 3.6;  // m/s → km/h
}

struct TrainerData
{
    double  powerWatts     = 0.0;   // instantaneous power (W)
    double  cadenceRpm     = 0.0;   // pedalling cadence (rpm)
    double  speedKph       = 0.0;   // calculated speed (km/h) – derived from power
    double  resistancePct  = 0.0;   // current resistance level 0-100 %
    bool    hasResistance  = false;
    bool    hasCadence     = false;
    bool    hasPower       = false;
};

struct HrmData
{
    int  heartRateBpm = 0;
    bool valid        = false;
};

// Protocol that provided the data
enum class DataSource { ANT, BLE, None };

