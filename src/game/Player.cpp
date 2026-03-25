// Player.cpp — All the physics that make first-person feel right.
// If something feels floaty, blame GRAVITY_A. If it feels sticky, blame dt clamping.
#include "Player.h"
#include <cmath>
#include <algorithm>

Player::Player() { reset(); }

void Player::reset() {
    pos       = SPAWN_POS;
    vel       = {};
    yaw       = 0.0f;
    pitch     = 0.0f;
    onGround  = false;
    prevGround= false;
    moveState = PMoveState::Idle;
    sprinting = false;
    energy    = ENERGY_MAX;
    bobPhase  = 0.0f;
    shakeAmt  = 0.0f;
    sprintFOV = 0.0f;
    fallFOV   = 0.0f;
}

glm::vec3 Player::forward() const {
    float yr = glm::radians(yaw), pr = glm::radians(pitch);
    return { cosf(pr)*sinf(yr), sinf(pr), cosf(pr)*cosf(yr) };
}
glm::vec3 Player::right() const {
    // GLM lookAt maps world+X to screen LEFT when forward=+Z.
    // Negate so pressing D (right) moves toward screen right.
    float yr = glm::radians(yaw);
    return { -cosf(yr), 0.0f, sinf(yr) };
}
glm::vec3 Player::eyePos() const {
    glm::vec3 eye = { pos.x, pos.y + EYE_OFFSET, pos.z };
    if (onGround && moveState != PMoveState::Idle)
        eye.y += sinf(bobPhase) * (sprinting ? 0.038f : 0.014f);
    if (shakeAmt > 0.0f) {
        float ang = (float)(rand() % 628) / 100.0f;
        eye.x += cosf(ang) * shakeAmt;
        eye.y += sinf(ang) * shakeAmt;
    }
    return eye;
}

void Player::update(const PlayerInput& input, float dt) {
    yaw   += input.mouseDX * 0.12f; // mouseDX/Y are set by Game from raw WM_MOUSEMOVE
    pitch  = std::clamp(pitch - input.mouseDY * 0.12f, -89.0f, 89.0f);

    bool moving = input.forward || input.back || input.left || input.right;
    updateEnergyAndState(input, moving, dt);
    applyMovement(input, dt);
    applyGravity(dt);
    applyGroundCollision();
    updateCameraEffects(dt);

    // Void respawn — if you somehow fall through the world, this is why
    if (pos.y < VOID_Y - 100.0f) reset();
}

void Player::updateEnergyAndState(const PlayerInput& in, bool moving, float dt) {
    prevGround = onGround;

    if (!onGround) {
        // Airborne: no sprinting, regen energy because you're not trying that hard
        moveState = PMoveState::Walking;
        sprinting = false;
        energy = std::min(ENERGY_MAX, energy + ENERGY_REGEN * dt);
    } else if (moving) {
        if (in.sprint && energy > ENERGY_MIN_SPRINT) {
            moveState = PMoveState::Sprinting;
            sprinting = true;
            energy = std::max(0.0f, energy - ENERGY_DRAIN * dt);
        } else {
            moveState = PMoveState::Walking;
            sprinting = false;
            energy = std::min(ENERGY_MAX, energy + ENERGY_REGEN * dt);
        }
    } else {
        // Standing still: no drain even if holding ctrl
        // This was the bug in v6.1 — Ctrl while idle drained stamina. Silly.
        moveState = PMoveState::Idle;
        sprinting = false;
        energy = std::min(ENERGY_MAX, energy + ENERGY_REGEN * dt);
    }
}

void Player::applyMovement(const PlayerInput& in, float dt) {
    float yr = glm::radians(yaw);
    glm::vec3 fwd = { sinf(yr), 0.0f, cosf(yr) };
    glm::vec3 rgt = { -cosf(yr), 0.0f, sinf(yr) }; // matches right() fix

    // Speed mult: 1.0 normally, higher when speed boost item is active.
    // Only applies to sprint speed — base walk speed is unchanged.
    float sprintMult = sprinting ? SPRINT_MULT * in.speedMult : 1.0f;
    float spd = (sprinting ? MOVE_SPEED * sprintMult : MOVE_SPEED) * dt;

    bool moved = false;
    if (in.forward) { pos += fwd * spd; moved = true; }
    if (in.back)    { pos -= fwd * spd; moved = true; }
    if (in.left)    { pos -= rgt * spd; moved = true; }
    if (in.right)   { pos += rgt * spd; moved = true; }

    if (moved && onGround)
        bobPhase += dt * (sprinting ? 13.0f : 8.0f);

    // Jump was handled in Game::onKeyDown so it edge-detects properly
    // (pressing space via GetAsyncKeyState would jump every frame — bad)
}

void Player::applyGravity(float dt) {
    vel.y -= GRAVITY_A * dt;
    vel.y  = std::max(vel.y, -TERMINAL_V);
    pos.y += vel.y * dt;
}

void Player::applyGroundCollision() {
    bool overGround = (std::abs(pos.x) < GROUND_HALF &&
                       std::abs(pos.z) < GROUND_HALF);
    if (overGround && pos.y - CAP_HALF_H <= GROUND_Y) {
        float impact = vel.y;
        pos.y = GROUND_Y + CAP_HALF_H;
        if (vel.y < 0.0f) vel.y = 0.0f;
        onGround = true;
        // Landing shake — harder landings shake more
        if (!prevGround && impact < -4.0f)
            shakeAmt = std::min(0.055f, (-impact - 4.0f) * 0.006f);
    } else {
        onGround = false;
    }
}

void Player::updateCameraEffects(float dt) {
    shakeAmt = std::max(0.0f, shakeAmt - dt * 0.9f);
    float tSp = (sprinting && onGround) ? 7.0f : 0.0f;
    sprintFOV += (tSp - sprintFOV) * std::min(1.0f, dt * 7.0f);
    float tFa = (vel.y < -5.0f) ? std::min(8.0f, (-vel.y - 5.0f) * 0.55f) : 0.0f;
    fallFOV   += (tFa - fallFOV) * std::min(1.0f, dt * 5.0f);
}
