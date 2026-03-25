#pragma once
// TitleCamera.h — The camera that makes the world look good before the player
// gets in and starts throwing cubes at everything.
// Smoothly transitions between 3 random world points every 5 seconds.

#include "../Common.h"

class TitleCamera {
public:
    void init();
    void update(float dt);

    glm::mat4 viewMatrix()  const;
    glm::mat4 projMatrix(float fov, float aspect) const;

    glm::vec3 eyePos() const { return m_currentEye; }

private:
    // Pick a random interesting look-at target within the world
    static glm::vec3 randomTarget(unsigned int& rng);
    static glm::vec3 computeEye(const glm::vec3& target);

    glm::vec3 m_currentEye    = {0,4,-5};
    glm::vec3 m_currentTarget = {0,1, 0};
    glm::vec3 m_nextEye       = {5,3, 8};
    glm::vec3 m_nextTarget    = {0,1, 0};
    float     m_lerpT         = 0.f;
    float     m_holdTimer     = 0.f;
    static constexpr float HOLD_TIME   = 5.0f; // seconds per target
    static constexpr float LERP_SPEED  = 0.8f; // lerp coefficient

    unsigned int m_rng = 0x12345678;
};
