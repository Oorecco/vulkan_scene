#pragma once
// ActiveEffects.h — Tracks all timed active item effects.
// One place to check if any effect is currently running and for how long.

#include "../Common.h"

struct ActiveEffects {
    // Speed boost
    bool  speedBoost     = false;
    float speedBoostTime = 0.f;
    float speedBoostMult = 1.8f;   // 80% bonus sprint speed

    // Gravity flip (on cube)
    bool  gravityFlipped  = false;
    float gravityFlipTime = 0.f;
    static constexpr float GRAVITY_FLIP_DUR = 4.0f;

    // Super launcher: next throw is 5x
    bool cubeLauncherArmed = false;

    // Coin shower visual
    bool  coinShower    = false;
    float coinTimer     = 0.f;
    float coinDuration  = 3.0f;
    double coinTotal    = 0.0;
    double coinPaid     = 0.0;

    // Diamond pulse (highlight hidden objects)
    bool  diamondPulse  = false;
    float diamondTimer  = 0.f;
    static constexpr float DIAMOND_PULSE_DUR = 8.0f;

    // Freeze time
    bool  freezeTime    = false;
    float freezeTimer   = 0.f;

    // Earthquake
    bool  earthquake    = false;
    float quakeTimer    = 0.f;

    // Black hole (gravity well near cube)
    bool  blackHole     = false;
    float blackHoleTimer= 0.f;
    static constexpr float BLACK_HOLE_DUR   = 3.5f;
    static constexpr float BLACK_HOLE_FORCE = 22.0f;

    // ── Tick all timers ──────────────────────────────────────────────────
    void update(float dt) {
        if (speedBoost) {
            speedBoostTime -= dt;
            if (speedBoostTime <= 0) { speedBoost = false; speedBoostTime = 0; }
        }
        if (gravityFlipped) {
            gravityFlipTime -= dt;
            if (gravityFlipTime <= 0) { gravityFlipped = false; }
        }
        if (coinShower) {
            coinTimer += dt;
            if (coinTimer >= coinDuration) { coinShower = false; coinTimer = 0; }
        }
        if (diamondPulse) {
            diamondTimer += dt;
            if (diamondTimer >= DIAMOND_PULSE_DUR) { diamondPulse = false; diamondTimer = 0; }
        }
        if (freezeTime) {
            freezeTimer -= dt;
            if (freezeTimer <= 0) { freezeTime = false; freezeTimer = 0; }
        }
        if (earthquake) {
            quakeTimer -= dt;
            if (quakeTimer <= 0) { earthquake = false; quakeTimer = 0; }
        }
        if (blackHole) {
            blackHoleTimer -= dt;
            if (blackHoleTimer <= 0) { blackHole = false; blackHoleTimer = 0; }
        }
    }

    // Current sprint multiplier bonus from speed boost
    float sprintMult() const {
        return speedBoost ? speedBoostMult : 1.0f;
    }

    // Fraction remaining for UI bars
    float speedFraction()   const { return speedBoost   ? std::clamp(speedBoostTime / 15.0f, 0.f, 1.f) : 0.f; }
    float freezeFraction()  const { return freezeTime   ? std::clamp(freezeTimer / 5.0f, 0.f, 1.f) : 0.f; }
    float quakeFraction()   const { return earthquake   ? std::clamp(quakeTimer / 3.0f, 0.f, 1.f) : 0.f; }
    float holeFraction()    const { return blackHole    ? std::clamp(blackHoleTimer / BLACK_HOLE_DUR, 0.f, 1.f) : 0.f; }
    float diamondFraction() const { return diamondPulse ? std::clamp(1.f - diamondTimer/DIAMOND_PULSE_DUR, 0.f, 1.f) : 0.f; }
};
