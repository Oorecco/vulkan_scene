// CrateSpawner.cpp — The mystery box department.
#include "CrateSpawner.h"
#include <cmath>
#include <algorithm>

// Simple LCG inline — doesn't need to be cryptographically secure,
// we just want cheap randomness without pulling in <random>
static float lcg_frand(unsigned int& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s & 0xFFFFFF) / (float)0xFFFFFF;
}

// ── Init ──────────────────────────────────────────────────────────────────
void CrateSpawner::init(unsigned int seed) {
    m_rng = seed;
    m_crates.clear();

    // Pre-spawn MAX_CRATES/2 crates so the world isn't empty on first frame
    int initialCount = MAX_CRATES / 2;
    for (int i = 0; i < initialCount; i++) {
        WorldCrate c;
        c.pos          = randomSpawnPos(m_rng);
        c.active       = true;
        c.bobOffset    = lcg_frand(m_rng) * 6.28f; // random phase per crate
        c.respawnDelay = MIN_RESPAWN + lcg_frand(m_rng) * (MAX_RESPAWN - MIN_RESPAWN);
        m_crates.push_back(c);
    }
}

// ── Random spawn position ─────────────────────────────────────────────────
glm::vec3 CrateSpawner::randomSpawnPos(unsigned int& rng) const {
    // Pick random angle + distance within world bounds
    // Avoid the very center and edges of the ground plane
    float angle = lcg_frand(rng) * 6.28318f;
    float dist  = SPAWN_MIN_R + lcg_frand(rng) * (SPAWN_RADIUS - SPAWN_MIN_R);

    glm::vec3 pos;
    pos.x = cosf(angle) * dist;
    pos.z = sinf(angle) * dist;
    pos.y = GROUND_Y + 0.5f; // sit on ground (half-height of crate = 0.5)

    // Keep within ground plane (GROUND_HALF - 2m border)
    float border = GROUND_HALF - 2.0f;
    pos.x = std::clamp(pos.x, -border, border);
    pos.z = std::clamp(pos.z, -border, border);

    return pos;
}

// ── Update ────────────────────────────────────────────────────────────────
void CrateSpawner::update(float dt, const glm::vec3& playerPos) {
    // Tick respawn timers for opened crates
    for (auto& c : m_crates) {
        // Bob animation (gentle hover so they're noticeable)
        c.bobOffset += dt * 1.8f;

        if (!c.active) {
            c.respawnTimer += dt;
            if (c.respawnTimer >= c.respawnDelay) {
                // Respawn at a new random position
                c.pos          = randomSpawnPos(m_rng);
                c.active       = true;
                c.respawnTimer = 0.f;
                c.bobOffset    = lcg_frand(m_rng) * 6.28f;
            }
        }

        // Update proximity prompt
        if (c.active) {
            float dx = playerPos.x - c.pos.x;
            float dz = playerPos.z - c.pos.z;
            float dist2 = dx*dx + dz*dz;
            c.playerNear = (dist2 <= INTERACT_R * INTERACT_R);
        } else {
            c.playerNear = false;
        }
    }

    // Periodically try to spawn new crates up to cap
    m_spawnCheckTimer += dt;
    if (m_spawnCheckTimer >= 5.0f) {
        m_spawnCheckTimer = 0.f;
        trySpawnNew(m_rng);
    }
}

void CrateSpawner::trySpawnNew(unsigned int& rng) {
    // Count active crates
    int activeCount = 0;
    for (auto& c : m_crates) {
        if (c.active) activeCount++;
    }

    // Spawn a new slot if we're under MAX and we have open slot count to add
    if (activeCount < MAX_CRATES && (int)m_crates.size() < MAX_CRATES) {
        WorldCrate c;
        c.pos          = randomSpawnPos(rng);
        c.active       = true;
        c.bobOffset    = lcg_frand(rng) * 6.28f;
        c.respawnDelay = MIN_RESPAWN + lcg_frand(rng) * (MAX_RESPAWN - MIN_RESPAWN);
        m_crates.push_back(c);
    }
}

// ── Nearest interactable ──────────────────────────────────────────────────
WorldCrate* CrateSpawner::nearestInteractable(const glm::vec3& playerPos) {
    WorldCrate* best    = nullptr;
    float       bestD2  = INTERACT_R * INTERACT_R;

    for (auto& c : m_crates) {
        if (!c.active || !c.playerNear) continue;
        float dx = playerPos.x - c.pos.x;
        float dz = playerPos.z - c.pos.z;
        float d2 = dx*dx + dz*dz;
        if (d2 < bestD2) { bestD2 = d2; best = &c; }
    }
    return best;
}

// ── Interact / open ───────────────────────────────────────────────────────
bool CrateSpawner::tryInteract(const glm::vec3& playerPos,
                                CrateReward& outReward,
                                bool diamondsUnlocked)
{
    WorldCrate* c = nearestInteractable(playerPos);
    if (!c) return false;

    // Open the crate
    c->active       = false;
    c->respawnTimer = 0.f;
    c->respawnDelay = MIN_RESPAWN + lcg_frand(m_rng) * (MAX_RESPAWN - MIN_RESPAWN);
    c->playerNear   = false;

    // Use a seed derived from position + rng so the same spot isn't always the same loot
    unsigned int lootSeed = m_rng ^ (unsigned int)(c->pos.x * 1000) ^ (unsigned int)(c->pos.z * 7777);
    outReward = m_itemSys.rollCrateReward(diamondsUnlocked, (int)lootSeed);

    if (onCrateOpened) onCrateOpened();

    // Advance RNG so next interaction is different
    lcg_frand(m_rng);
    return true;
}
