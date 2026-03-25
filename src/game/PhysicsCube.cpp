// PhysicsCube.cpp
#include "PhysicsCube.h"
#include <algorithm>

PhysicsCube::PhysicsCube() {}

// ── Rotated AABB extents ─────────────────────────────────────────────────
// Converts the oriented box into an axis-aligned bounding box.
// This is what makes the hitbox match the visual model instead of a sphere.
glm::vec3 PhysicsCube::aabbExtents() const {
    glm::mat3 r = glm::mat3_cast(rotation);
    // For each world axis, the AABB extent is the sum of abs projections of all box axes
    float hs = PC_HALF;
    glm::vec3 ext;
    ext.x = hs * (std::abs(r[0][0]) + std::abs(r[1][0]) + std::abs(r[2][0]));
    ext.y = hs * (std::abs(r[0][1]) + std::abs(r[1][1]) + std::abs(r[2][1]));
    ext.z = hs * (std::abs(r[0][2]) + std::abs(r[1][2]) + std::abs(r[2][2]));
    return ext;
}

glm::mat4 PhysicsCube::worldMatrix() const {
    glm::mat4 m = glm::mat4_cast(rotation);
    m[3] = glm::vec4(pos, 1.0f);
    return m;
}

float PhysicsCube::bsRadius() const {
    return glm::root_three<float>() * PC_HALF + 0.3f;
}

// ── Integrate quaternion rotation ─────────────────────────────────────────
void PhysicsCube::integrateRotation(float dt) {
    float angle = glm::length(angVel) * dt;
    if (angle < 1e-5f) return;
    glm::vec3 axis = glm::normalize(angVel);
    glm::quat dq   = glm::angleAxis(angle, axis);
    rotation       = glm::normalize(dq * rotation);
}

// ── Main physics update (called when not grabbed) ─────────────────────────
void PhysicsCube::update(float dt) {
    pushCD = std::max(0.0f, pushCD - dt);

    // Apply gravity to velocity
    vel.y -= GRAVITY_A * dt;
    vel.y  = std::max(vel.y, -TERMINAL_V);

    // Sub-stepping: prevents tunnelling through the ground at high speed
    float speed = glm::length(vel);
    int steps = std::clamp((int)(speed * dt / 0.08f) + 1, 1, 4);
    float sdt = dt / (float)steps;

    for (int s = 0; s < steps; ++s) {
        pos += vel * sdt;

        // ── Ground collision ──────────────────────────────────────────────
        glm::vec3 ext = aabbExtents();
        float minY = GROUND_Y + ext.y;

        bool overGround = (std::abs(pos.x) < GROUND_HALF &&
                           std::abs(pos.z) < GROUND_HALF);

        if (overGround && pos.y <= minY) {
            pos.y = minY;  // hard clamp — no noclip
            if (vel.y < 0.0f) vel.y *= -0.42f; // bounce

            // Ground friction (exponential decay per sub-step)
            float fr = std::pow(0.12f, sdt);
            vel.x *= fr;
            vel.z *= fr;

            // Angular friction on ground contact
            float af = std::pow(0.22f, sdt);
            angVel.x *= af;
            angVel.z *= af;
            angVel.y *= std::pow(0.40f, sdt);
        }
    }

    // Void respawn — keep X/Z, sample ground height (simplified: ground = 0)
    if (pos.y < VOID_Y) {
        respawn();
        return;
    }

    // Air angular damping
    float af = std::pow(0.72f, dt);
    angVel *= af;

    integrateRotation(dt);
}

// ── Push impulse ──────────────────────────────────────────────────────────
void PhysicsCube::applyPushImpulse(const glm::vec3& dir) {
    vel    += dir * PUSH_IMPULSE;
    // Add angular spin from the push direction
    angVel += glm::vec3(dir.z * 4.0f - dir.y * 2.0f,
                        dir.x * 3.0f,
                        dir.y * 2.0f - dir.x * 4.0f);
    pushCD  = PUSH_CD_TIME;
}

// ── Grab update ───────────────────────────────────────────────────────────
void PhysicsCube::updateGrab(const glm::vec3& targetPos, float dt) {
    // Track velocity for throw estimation
    if (dt > 0.0f) {
        glm::vec3 rawVel = (targetPos - pos) / dt;
        grabVelSmooth = glm::mix(grabVelSmooth, rawVel, 0.25f);
    }
    prevGrabPos = pos;
    pos = targetPos;
    vel = {};

    // Damp rotation while held
    float gf = std::pow(0.15f, dt);
    angVel *= gf;
    integrateRotation(dt);
}

// ── Release / throw ───────────────────────────────────────────────────────
void PhysicsCube::release() {
    grabbed = false;
    // Apply smoothed throw velocity (capped so it's not unreasonably powerful)
    float maxThrow = 18.0f;
    glm::vec3 throwVel = grabVelSmooth * 2.2f;
    if (glm::length(throwVel) > maxThrow)
        throwVel = glm::normalize(throwVel) * maxThrow;
    vel = throwVel;
    grabVelSmooth = {};
}

// ── Respawn ───────────────────────────────────────────────────────────────
void PhysicsCube::respawn() {
    // Keep X/Z with slight random offset, reset to ground height
    float ox = ((float)(rand() % 200) / 100.0f - 1.0f) * 0.5f;
    float oz = ((float)(rand() % 200) / 100.0f - 1.0f) * 0.5f;
    pos      = { pos.x + ox, PC_HALF, pos.z + oz };
    vel      = {};
    angVel   = {};
    rotation = glm::identity<glm::quat>();
    grabbed  = false;
}
