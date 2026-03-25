#pragma once
// PhysicsCube.h — The orange cube with proper rotated AABB hitbox.
// Quaternion rotation, sub-step physics, throw velocity tracking.

#include "../Common.h"

class PhysicsCube {
public:
    PhysicsCube();

    void update(float dt);   // call only when NOT grabbed

    // State
    glm::vec3 pos      = { 3.5f, PC_HALF, 1.0f };
    glm::vec3 vel      = {};
    glm::vec3 angVel   = {};
    glm::quat rotation = glm::identity<glm::quat>();
    bool  grabbed      = false;
    float pushCD       = 0.0f;

    // Grab / throw velocity tracking
    glm::vec3 grabVelSmooth = {};
    glm::vec3 prevGrabPos   = {};

    glm::mat4  worldMatrix()  const;
    float      bsRadius()     const;
    glm::vec3  aabbExtents()  const; // axis-aligned extents from rotation
    glm::vec3  aabbMin()      const { return pos - aabbExtents(); }
    glm::vec3  aabbMax()      const { return pos + aabbExtents(); }

    void applyPushImpulse(const glm::vec3& dir);
    void updateGrab(const glm::vec3& targetPos, float dt);
    void release();
    void respawn();

private:
    void integrateRotation(float dt);
};
