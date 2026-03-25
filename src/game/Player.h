#pragma once
// Player.h — First-person player controller. Fixed movement state logic.
// Bug fixes vs v6.1:
//   - Idle + ctrl held = no energy drain (was draining before)
//   - Sprint only from Walking state, not from Idle
//   - Cursor released properly on focus loss (handled in Game)
// Part 4 addition: speedMult field so ActiveEffects can boost sprint speed
// without poking at constants directly.

#include "../Common.h"

struct PlayerInput {
    bool  forward = false;
    bool  back    = false;
    bool  left    = false;
    bool  right   = false;
    bool  sprint  = false;
    float mouseDX = 0.0f; // raw pixel delta this frame — applied in Player::update()
    float mouseDY = 0.0f;
    // speedMult: 1.0 = normal, >1.0 = boosted (from speed boost item)
    float speedMult = 1.0f;
};

class Player {
public:
    Player();
    void reset();

    // Main update: mouse look + movement + gravity + camera effects
    void update(const PlayerInput& input, float dt);

    // ── Exposed state (Game reads/writes some directly — not ideal but pragmatic) ──
    glm::vec3  pos        = SPAWN_POS;
    glm::vec3  vel        = {};
    float      yaw        = 0.0f;   // degrees, wraps naturally
    float      pitch      = 0.0f;   // degrees, clamped ±89
    bool       onGround   = false;
    bool       prevGround = false;
    PMoveState moveState  = PMoveState::Idle;
    bool       sprinting  = false;
    float      energy     = ENERGY_MAX;

    // Camera effects (read by Game to compute FOV and eye position)
    float bobPhase  = 0.0f;
    float shakeAmt  = 0.0f;
    float sprintFOV = 0.0f;
    float fallFOV   = 0.0f;

    // Helpers
    glm::vec3 eyePos()  const;
    glm::vec3 forward() const;
    glm::vec3 right()   const;

private:
    void applyMovement(const PlayerInput& in, float dt);
    void applyGravity(float dt);
    void applyGroundCollision();
    void updateEnergyAndState(const PlayerInput& in, bool moving, float dt);
    void updateCameraEffects(float dt);
};
