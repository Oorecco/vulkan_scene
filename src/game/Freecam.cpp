// Freecam.cpp — Floaty but controlled. Physics-like camera movement.
#include "Freecam.h"
#include <windows.h>
#include <cmath>

static bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void Freecam::enable(const glm::vec3& startPos, float startYaw, float startPitch) {
    m_state.pos   = startPos;
    m_state.yaw   = startYaw;
    m_state.pitch = startPitch;
    m_state.vel   = { 0, 0, 0 };
    m_active = true;
}

void Freecam::disable() {
    m_active = false;
    m_state.vel = { 0, 0, 0 };
}

void Freecam::applyMouseDelta(int dx, int dy, float sensitivity) {
    m_state.yaw   -= (float)dx * sensitivity; // negate: match Player yaw convention
    m_state.pitch  = std::clamp(m_state.pitch - (float)dy * sensitivity,
                                -89.0f, 89.0f);
}

void Freecam::update(float dt) {
    if (!m_active) return;

    float yr = glm::radians(m_state.yaw);
    float pr = glm::radians(m_state.pitch);

    glm::vec3 fwd   = { cosf(pr)*sinf(yr), sinf(pr), cosf(pr)*cosf(yr) };
    glm::vec3 up    = { 0, 1, 0 };
    glm::vec3 right = glm::normalize(glm::cross(up, fwd));

    // Build target velocity from input
    glm::vec3 inputDir = { 0, 0, 0 };
    if (key('W'))       inputDir += fwd;
    if (key('S'))       inputDir -= fwd;
    if (key('A'))       inputDir += right;
    if (key('D'))       inputDir -= right;
    if (key('Q'))       inputDir += up;
    if (key('E'))       inputDir -= up;

    // Accelerate toward input direction, decelerate when no input
    if (glm::length(inputDir) > 0.1f) {
        inputDir = glm::normalize(inputDir);
        m_state.vel += inputDir * (ACCEL * dt);
        // Clamp to max speed
        float spd = glm::length(m_state.vel);
        if (spd > MAX_SPD)
            m_state.vel = (m_state.vel / spd) * MAX_SPD;
    } else {
        // Smooth deceleration
        float spd = glm::length(m_state.vel);
        if (spd > 0.01f) {
            float newSpd = std::max(0.0f, spd - DECEL * dt);
            m_state.vel = (m_state.vel / spd) * newSpd;
        } else {
            m_state.vel = { 0, 0, 0 };
        }
    }

    m_state.pos += m_state.vel * dt;
}

glm::mat4 Freecam::viewMatrix() const {
    float yr = glm::radians(m_state.yaw);
    float pr = glm::radians(m_state.pitch);
    glm::vec3 fwd = { cosf(pr)*sinf(yr), sinf(pr), cosf(pr)*cosf(yr) };
    return glm::lookAt(m_state.pos, m_state.pos + fwd, glm::vec3(0, 1, 0));
}
