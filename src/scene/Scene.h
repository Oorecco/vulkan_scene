#pragma once
// scene/Scene.h — World geometry + frustum culling.
#include "../Common.h"
#include "../renderer/VulkanRenderer.h"
#include "../renderer/VulkanBuffer.h"
#include "../game/PhysicsCube.h"
#include "../game/Player.h"

struct Mesh {
    VulkanBuffer vb, ib;
    uint32_t  idxCount = 0;
    glm::vec3 bsCenter = {};
    float     bsRadius = 1.0f;
    glm::mat4 world    = glm::mat4(1.0f);
    void destroy(VkDevice dev) { vb.destroy(dev); ib.destroy(dev); }
};

class Scene {
public:
    void init(const VulkanContext& ctx);
    // Set ground diffuse texture after assets are loaded
    void setGroundTexture(VkDescriptorSet ds) { m_groundTexSet = ds; }
    void update(float simTime, const Player& player,
                const PhysicsCube& cube, bool freecamActive);
    void draw(VulkanRenderer& r, bool shadow);
    void drawCube(VulkanRenderer& r, bool shadow);
    void drawCapsule(VulkanRenderer& r, bool shadow);
    void destroy(VkDevice dev);

    Frustum frustum = {};

private:
    Mesh buildColorCube(const VulkanContext& ctx);
    Mesh buildOrangeCube(const VulkanContext& ctx);
    Mesh buildGround(const VulkanContext& ctx);
    Mesh buildCapsule(const VulkanContext& ctx);
    bool sphereVisible(const glm::vec3& c, float r) const;
    void tryDraw(VulkanRenderer& r, Mesh& m, bool shadow);

    Mesh m_displayCube, m_physicsCube, m_ground, m_capsule;
    VkDescriptorSet m_groundTexSet = VK_NULL_HANDLE;
};
