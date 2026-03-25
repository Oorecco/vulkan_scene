#pragma once
// Freecam.h — Free camera with smooth acceleration + deceleration.
// Toggle: Shift + P (changed from DX version's Shift+K+F1 — cleaner).

#include "../Common.h"

class Freecam {
public:
    struct State {
        glm::vec3 pos     = { 0, 3, -8 };
        float     yaw     = 0.0f;
        float     pitch   = 0.0f;
        // Smooth velocity — not instant speed changes
        glm::vec3 vel     = { 0, 0, 0 };
    };

    Freecam() = default;

    bool isActive() const { return m_active; }

    // Toggle on/off. When enabling, inherits player camera position.
    void enable(const glm::vec3& startPos, float startYaw, float startPitch);
    void disable();

    // Per-frame update (reads keyboard, smooths velocity)
    void update(float dt);

    // Apply mouse delta
    void applyMouseDelta(int dx, int dy, float sensitivity = 0.12f);

    // View matrix
    glm::mat4 viewMatrix() const;

    const State& state() const { return m_state; }

    // Teleport player to freecam position (Shift+R)
    glm::vec3 playerTeleportPos() const { return m_state.pos; }

private:
    State m_state;
    bool  m_active = false;

    static constexpr float ACCEL   = 28.0f; // speed gain per second
    static constexpr float DECEL   = 16.0f; // speed loss per second
    static constexpr float MAX_SPD = 12.0f; // max movement speed
};
