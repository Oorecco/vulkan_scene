#pragma once
// WorldDecorations.h — Dense world clutter: trees, rocks, mushrooms, crates, fences.
// Part 4: now textured. Each prop type has a diffuse descriptor set created from
// AssetManager on init. Untextured fallback still works if asset load fails.

#include "../Common.h"
#include "../renderer/VulkanBuffer.h"
#include "../renderer/VulkanContext.h"
#include "../renderer/VulkanRenderer.h"
#include "../renderer/AssetManager.h"
#include <vector>
#include <unordered_map>
#include <string>

// A single prop instance in the world
struct PropInstance {
    glm::vec3 pos;
    float     yaw;       // rotation around Y
    float     scale;
    glm::mat4 world;     // computed once
    glm::vec3 bsCenter;
    float     bsRadius;

    // Collision AABB (axis-aligned, no rotation — good enough for most props)
    bool      hasCollision = true;
    glm::vec3 aabbMin, aabbMax; // world-space AABB
};

// A prop type: shared mesh + per-instance transforms
struct PropMesh {
    VulkanBuffer vb, ib;
    uint32_t     idxCount = 0;
    std::string  textureName; // subpath into AssetManager
    bool         doubleSided = false; // 2D grass, leaves need this
    void destroy(VkDevice dev) { vb.destroy(dev); ib.destroy(dev); }
};

enum class PropType {
    Tree, Rock, Mushroom, Crate, FencePost, FenceRail,
    GrassBillboard, Bush, Stone, Sign,
    COUNT
};

class WorldDecorations {
public:
    void init(const VulkanContext& ctx, VulkanRenderer& renderer,
              AssetManager& assets, unsigned int seed = 1337);
    void destroy(VkDevice dev);

    // Draw all props (shadow pass or main pass)
    // Frustum is checked per-instance
    void draw(VulkanRenderer& renderer,
              const Frustum& frustum,
              const glm::vec3& camEye,
              bool shadow);

    // Resolve player capsule against all collidable prop AABBs
    // Modifies playerPos if overlapping
    void resolvePlayerCollision(glm::vec3& playerPos, float capsuleRadius, float capsuleHalfH) const;

    // Get all collidable AABBs (for physics cube collision — checked in Game)
    const std::vector<PropInstance>& allInstances() const { return m_allInstances; }

    int drawnCount()  const { return m_drawnCount; }
    int culledCount() const { return m_culledCount; }

private:
    // Mesh builders
    PropMesh buildTreeTrunk(const VulkanContext& ctx);
    PropMesh buildTreeCone(const VulkanContext& ctx);  // stylised low-poly cone canopy
    PropMesh buildRock(const VulkanContext& ctx, float scale);
    PropMesh buildMushroom(const VulkanContext& ctx);
    PropMesh buildMushroomCap(const VulkanContext& ctx);
    PropMesh buildCrateMesh(const VulkanContext& ctx);
    PropMesh buildFencePost(const VulkanContext& ctx);
    PropMesh buildFenceRail(const VulkanContext& ctx);
    PropMesh buildGrassQuad(const VulkanContext& ctx); // 2D crossed quads
    PropMesh buildBush(const VulkanContext& ctx);
    PropMesh buildStone(const VulkanContext& ctx);
    PropMesh buildSignPost(const VulkanContext& ctx);

    // Placement generators
    void placeProps(unsigned int seed);
    void placeTreeCluster(unsigned int& rng, glm::vec2 center, int count);
    void placeRockScatter(unsigned int& rng, int count);
    void placeMushrooms(unsigned int& rng, int count);
    void placeGrassFields(unsigned int& rng, int count);
    void placeFences(unsigned int& rng);
    void placeCrates(unsigned int& rng, int count); // decorative (NOT world crates)
    void placeSigns(unsigned int& rng, int count);

    // Frustum test (bounding sphere)
    bool visible(const Frustum& f, const glm::vec3& c, float r) const;

    // Draw a single mesh with a given transform
    void drawProp(VulkanRenderer& r,
                  const PropMesh& mesh,
                  const glm::mat4& world,
                  bool shadow,
                  bool doubleSided = false);

    // All prop meshes (indexed by PropType)
    PropMesh m_meshes[(int)PropType::COUNT];

    // All placed instances, grouped by PropType for batching
    std::vector<std::vector<PropInstance>> m_byType; // m_byType[PropType]

    // Flat list of ALL instances for collision checks
    std::vector<PropInstance> m_allInstances;

    // Texture descriptor set cache keyed by texture subpath
    std::unordered_map<std::string, VkDescriptorSet> m_texSets;
    VulkanRenderer* m_renderer = nullptr;

    mutable int m_drawnCount  = 0;
    mutable int m_culledCount = 0;

    static float lcgF(unsigned int& s) {
        s = s * 1664525u + 1013904223u;
        return (float)(s & 0xFFFFFF) / (float)0xFFFFFF;
    }
    static glm::vec3 randPosOnGround(unsigned int& rng, float minR, float maxR) {
        float ang  = lcgF(rng) * 6.2832f;
        float dist = minR + lcgF(rng) * (maxR - minR);
        return { cosf(ang)*dist, GROUND_Y, sinf(ang)*dist };
    }
};
