// TitleCamera.cpp — Making the game look better than it actually is since 2025.
#include "TitleCamera.h"
#include <cmath>

static float lcgF(unsigned int& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s & 0xFFFFFF) / (float)0xFFFFFF;
}

// Good-looking world points to orbit around — scaled for 440m world.
// Mix of near-spawn interest and far dramatic vistas.
static const glm::vec3 INTERESTING_TARGETS[] = {
    {   0.0f, 1.0f,   0.0f }, // display cube area — the heart of it all
    {  30.0f, 0.5f,  25.0f }, // east clearing
    { -45.0f, 0.3f, -40.0f }, // southwest open field
    {  60.0f, 0.3f, -55.0f }, // southeast tree zone
    { -80.0f, 0.3f,  70.0f }, // northwest forest edge
    { 100.0f, 0.3f,  10.0f }, // far east horizon
    { -20.0f, 0.3f, 110.0f }, // north deep (likely near a tree cluster)
    {  0.0f,  0.3f, -90.0f }, // south long run
    {-110.0f, 0.3f, -60.0f }, // far southwest
    { 90.0f,  0.3f,  80.0f }, // far northeast
};
static constexpr int TARGET_COUNT = 10;

glm::vec3 TitleCamera::randomTarget(unsigned int& rng) {
    int idx = (int)(lcgF(rng) * TARGET_COUNT) % TARGET_COUNT;
    return INTERESTING_TARGETS[idx];
}

glm::vec3 TitleCamera::computeEye(const glm::vec3& target) {
    // Different orbit angles per target so consecutive views look distinct.
    // Use target position as a seed for a varied approach angle.
    unsigned int s = (unsigned int)(std::abs(target.x)*100.f) + (unsigned int)(std::abs(target.z)*100.f) + 1;
    s = s * 1664525u + 1013904223u;
    float ang  = ((float)(s & 0xFFFF) / 65535.f) * 6.2832f;
    float dist = 12.0f + ((float)((s>>16) & 0xFF) / 255.f) * 8.0f; // 12-20m from target
    float elev = 4.0f  + ((float)((s>>8)  & 0xFF) / 255.f) * 5.0f; // 4-9m high
    return target + glm::vec3(cosf(ang)*dist, elev, sinf(ang)*dist);
}

void TitleCamera::init() {
    m_currentTarget = randomTarget(m_rng);
    m_currentEye    = computeEye(m_currentTarget);
    m_nextTarget    = randomTarget(m_rng);
    m_nextEye       = computeEye(m_nextTarget);
    m_lerpT         = 0.f;
    m_holdTimer     = 0.f;
}

void TitleCamera::update(float dt) {
    m_holdTimer += dt;

    if (m_holdTimer < HOLD_TIME) {
        // Hold the current view — nothing to do
        return;
    }

    // Lerp toward next target
    m_lerpT += dt * LERP_SPEED;
    if (m_lerpT >= 1.0f) {
        // Arrived at next target — it becomes current, pick a new next
        m_currentEye    = m_nextEye;
        m_currentTarget = m_nextTarget;
        m_nextTarget    = randomTarget(m_rng);
        m_nextEye       = computeEye(m_nextTarget);
        // Make sure we don't pick the same target twice in a row
        // (won't look like anything is happening)
        if (glm::distance(m_nextTarget, m_currentTarget) < 1.0f)
            m_nextTarget = randomTarget(m_rng);
        m_lerpT         = 0.f;
        m_holdTimer     = 0.f;
    } else {
        // Smooth step for ease-in/out — because linear lerp looks like a security camera
        float t = m_lerpT;
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        m_currentEye    = glm::mix(m_currentEye,    m_nextEye,    t * dt * LERP_SPEED * 0.5f);
        m_currentTarget = glm::mix(m_currentTarget, m_nextTarget, t * dt * LERP_SPEED * 0.5f);
    }
}

glm::mat4 TitleCamera::viewMatrix() const {
    return glm::lookAt(m_currentEye, m_currentTarget, glm::vec3(0,1,0));
}

glm::mat4 TitleCamera::projMatrix(float fov, float aspect) const {
    glm::mat4 p = glm::perspective(glm::radians(fov), aspect, NEAR_Z, 300.0f);
    p[1][1] *= -1.0f; // Vulkan Y flip
    return p;
}
