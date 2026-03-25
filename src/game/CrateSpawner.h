#pragma once
// CrateSpawner.h — Wooden crates that spawn randomly around the world.
// Interact with E to open. Contains random money, diamonds, or unique items.
// They respawn after a random interval and there's always a cap on active crates.

#include "../Common.h"
#include "ItemSystem.h"
#include <vector>
#include <functional>

// One world crate instance
struct WorldCrate {
    glm::vec3 pos;
    bool      active     = true;  // false = opened, waiting to respawn
    float     respawnTimer = 0.f; // countdown before it reappears
    float     respawnDelay = 0.f; // randomised per-crate interval
    bool      playerNear  = false;// interaction prompt visible
    float     bobOffset   = 0.f;  // gentle hover bob phase
};

class CrateSpawner {
public:
    // Config
    static constexpr int   MAX_CRATES   = 8;   // max active crates at once
    static constexpr float MIN_RESPAWN  = 30.f; // minimum seconds before respawn
    static constexpr float MAX_RESPAWN  = 90.f; // maximum seconds before respawn
    static constexpr float INTERACT_R   = 2.8f; // interaction radius
    static constexpr float SPAWN_RADIUS = 18.f; // how far from center to spawn
    static constexpr float SPAWN_MIN_R  =  4.f; // minimum from center (not right on top of player)

    void init(unsigned int seed = 42);
    void update(float dt, const glm::vec3& playerPos);

    // Try to open the nearest crate — returns true if one was opened
    // reward is filled with what the player gets
    bool tryInteract(const glm::vec3& playerPos, CrateReward& outReward,
                     bool diamondsUnlocked);

    // Returns closest active crate within INTERACT_R, nullptr if none
    WorldCrate* nearestInteractable(const glm::vec3& playerPos);

    // Getters for rendering
    const std::vector<WorldCrate>& crates() const { return m_crates; }

    // Open sound callback
    std::function<void()> onCrateOpened;

private:
    glm::vec3 randomSpawnPos(unsigned int& rng) const;
    void trySpawnNew(unsigned int& rng);

    std::vector<WorldCrate> m_crates;
    unsigned int            m_rng     = 0;
    float                   m_spawnCheckTimer = 0.f;
    ItemSystem              m_itemSys;  // used internally only for rolling
};
