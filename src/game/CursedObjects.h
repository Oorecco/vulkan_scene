#pragma once
// CursedObjects.h — Five hidden objects with corrupted textures scattered around the world.
// 3 only appear at night. 2 only during the day. Because that's just how curses work.
// Find all 5 → trigger IT'S NOT YOUR FAULT → permanently unlock diamonds.
// Interact with E.

#include "../Common.h"
#include "../renderer/VulkanRenderer.h"
#include "../renderer/VulkanBuffer.h"
#include "../renderer/VulkanContext.h"
#include "DayNight.h"
#include <vector>
#include <functional>

struct CursedObject {
    int       id;           // 0–4
    glm::vec3 pos;
    bool      nightOnly;    // true = only visible/interactable at night
    bool      found;        // permanently saved
    bool      visible;      // currently shown (depends on time of day)
    float     glitchPhase;  // animation phase for shader effect
    float     bobPhase;

    // Interaction radius
    static constexpr float INTERACT_R = 2.5f;
};

class CursedObjects {
public:
    // Pass in which objects have already been found (from save data)
    void init(const VulkanContext& ctx, const bool foundFlags[5]);
    void destroy(VkDevice dev);

    // Update visibility based on time of day
    // Returns true if any state changed (to trigger save)
    bool update(float dt, const glm::vec3& playerPos, const DayNight& time,
                bool diamondPulseActive);

    // Try to interact with the nearest visible + unfound object
    // Returns id (0–4) if interacted, -1 otherwise
    int tryInteract(const glm::vec3& playerPos);

    // Number of found objects
    int foundCount() const;

    // True if all 5 are found
    bool allFound() const { return foundCount() >= 5; }

    // For rendering
    const std::vector<CursedObject>& objects() const { return m_objects; }

    // Called on find
    std::function<void(int id, bool wasLast)> onFound;

    // Draw the glitchy objects
    void draw(VulkanRenderer& renderer, bool shadow);
    // Set the corrupted texture descriptor (called from Game after assets are loaded)
    void setTexture(VkDescriptorSet ds);

    // Is player near any visible unfound object (for HUD prompt)
    bool playerNearAny() const { return m_playerNear; }

private:
    std::vector<CursedObject> m_objects;
    VulkanBuffer    m_vb, m_ib;
    uint32_t        m_idxCount = 0;
    VkDescriptorSet m_texSet   = VK_NULL_HANDLE; // cursed_geometry.png descriptor
    bool         m_playerNear = false;

    void buildMesh(const VulkanContext& ctx);
};
