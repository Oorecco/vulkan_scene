// CursedObjects.cpp — "Why is that rock flickering?" — your players, eventually.
#include "CursedObjects.h"
#include <algorithm>
#include <cmath>

// Object positions are hand-placed for good hiding spots.
// Night-only (ids 0,1,2): tucked in corners, under trees, far edge.
// Day-only (ids 3,4): in plain sight but glitch effect is subtle in daylight.
// Positions for 750x750m world (GROUND_HALF=375).
// All objects are 200-330m from spawn — far enough that you must explore to find them,
// close enough that you're not walking for 10 real minutes to interact.
// Night objects (0-2): hidden in tree-heavy zones, corners, dark areas.
// Day objects (3-4): in the open but NOT near spawn — easy to see IF you walk there.
static const glm::vec3 POSITIONS[5] = {
    { -245.0f, GROUND_Y + 0.3f,  200.0f },  // id=0 night: deep northwest, behind tree line
    {  280.0f, GROUND_Y + 0.3f, -210.0f },  // id=1 night: far southeast — longest walk
    {  -30.0f, GROUND_Y + 0.3f,  300.0f },  // id=2 night: north far edge, needs night eyes
    {  195.0f, GROUND_Y + 0.3f,  130.0f },  // id=3 day: northeast open field, visible but far
    { -200.0f, GROUND_Y + 0.3f, -155.0f },  // id=4 day: southwest clearing, no cover
};
static const bool NIGHT_ONLY[5] = { true, true, true, false, false };

void CursedObjects::init(const VulkanContext& ctx, const bool foundFlags[5]) {
    m_objects.clear();
    for (int i = 0; i < 5; i++) {
        CursedObject o;
        o.id        = i;
        o.pos       = POSITIONS[i];
        o.nightOnly = NIGHT_ONLY[i];
        o.found     = foundFlags[i];
        o.visible   = false;
        o.glitchPhase = (float)i * 1.27f; // stagger glitch animations
        o.bobPhase    = (float)i * 0.88f;
        m_objects.push_back(o);
    }
    buildMesh(ctx);
}

void CursedObjects::buildMesh(const VulkanContext& ctx) {
    // Shape: a standing monolith — a tall, slightly tapered hexagonal prism.
    // Real-world inspiration: ancient standing stones / dolmens.
    // Then some vertices are displaced chaotically to look "corrupted".
    // With cursed_geometry.png mapped on it, the result is unsettling but
    // not immediately obviously "a game object someone forgot to skin".
    const int   SIDES   = 6;     // hexagonal cross-section
    const float BOT_R   = 0.28f; // base radius
    const float TOP_R   = 0.18f; // slightly tapered top (more monolith-like)
    const float HEIGHT  = 0.85f; // tall: 85cm — stands out against ground
    const float HALF_H  = HEIGHT * 0.5f;

    // Deterministic corruption function — same result every run, but looks random
    unsigned int rng_s = 0xDEADBEEF;
    auto lcg = [&]() -> float {
        rng_s = rng_s * 1664525u + 1013904223u;
        return (float)(rng_s & 0xFFFF) / 65535.f * 2.f - 1.f;
    };
    // Corruption magnitudes: most verts barely move, a few are visibly wrong
    // Index 0-5=bottom ring, 6-11=top ring, then cap verts
    // Corrupt magnitude for each vert index (pre-generated)
    float corrupt_mag[14] = {
        0.01f, 0.18f, 0.02f, 0.01f, 0.22f, 0.01f,  // bottom ring
        0.01f, 0.01f, 0.25f, 0.01f, 0.01f, 0.19f,  // top ring
        0.08f, 0.03f                                 // cap centers
    };

    std::vector<Vertex3D> verts;
    std::vector<uint32_t> idxs;
    glm::vec3 white{1.f, 1.f, 1.f};

    // Build bottom and top ring vertices
    std::vector<glm::vec3> bot_pts, top_pts;
    for (int i = 0; i <= SIDES; i++) {
        float a  = (float)i / SIDES * 6.2832f;
        float bx = cosf(a) * BOT_R, bz = sinf(a) * BOT_R;
        float tx = cosf(a) * TOP_R, tz = sinf(a) * TOP_R;
        // Apply corruption to ring positions (use pre-set magnitudes by index)
        float bot_m = corrupt_mag[i % 6];
        float top_m = corrupt_mag[6 + i % 6];
        bot_pts.push_back({bx + lcg()*bot_m, -HALF_H + lcg()*0.04f, bz + lcg()*bot_m});
        top_pts.push_back({tx + lcg()*top_m,  HALF_H + lcg()*top_m*0.5f, tz + lcg()*top_m});
    }

    // Side faces (quad strip around hex)
    for (int i = 0; i < SIDES; i++) {
        uint32_t b = (uint32_t)verts.size();
        glm::vec3 bp0 = bot_pts[i], bp1 = bot_pts[i+1];
        glm::vec3 tp0 = top_pts[i], tp1 = top_pts[i+1];
        // Normal: average of edge midpoints (not perfectly accurate — intentionally off)
        glm::vec3 mid_b = (bp0 + bp1) * 0.5f, mid_t = (tp0 + tp1) * 0.5f;
        glm::vec3 normal = glm::normalize(glm::cross(tp0 - bp0, bp1 - bp0));
        // UV: wrap horizontally, 0 at bottom 1 at top
        float u0 = (float)i / SIDES, u1 = (float)(i+1) / SIDES;
        verts.push_back({bp0, normal, white, {u0, 1.f}});
        verts.push_back({bp1, normal, white, {u1, 1.f}});
        verts.push_back({tp1, normal, white, {u1, 0.f}});
        verts.push_back({tp0, normal, white, {u0, 0.f}});
        idxs.insert(idxs.end(), {b, b+1, b+2,  b, b+2, b+3});
    }

    // Top cap (fan from center) — slightly corrupted center point
    {
        glm::vec3 cap_center = {lcg()*corrupt_mag[12], HALF_H + corrupt_mag[12]*0.5f, lcg()*0.05f};
        uint32_t ci = (uint32_t)verts.size();
        verts.push_back({cap_center, {0,1,0}, white, {0.5f, 0.5f}});
        for (int i = 0; i < SIDES; i++) {
            uint32_t b = (uint32_t)verts.size();
            float a0 = (float)i/SIDES*6.2832f, a1 = (float)(i+1)/SIDES*6.2832f;
            glm::vec2 uv0{cosf(a0)*0.5f+0.5f, sinf(a0)*0.5f+0.5f};
            glm::vec2 uv1{cosf(a1)*0.5f+0.5f, sinf(a1)*0.5f+0.5f};
            verts.push_back({top_pts[i],   {0,1,0}, white, uv0});
            verts.push_back({top_pts[i+1], {0,1,0}, white, uv1});
            idxs.insert(idxs.end(), {ci, b, b+1});
        }
    }

    // Bottom cap (fan downward, hidden but needed for shadows)
    {
        glm::vec3 cap_center = {lcg()*0.02f, -HALF_H, lcg()*0.02f};
        uint32_t ci = (uint32_t)verts.size();
        verts.push_back({cap_center, {0,-1,0}, white, {0.5f,0.5f}});
        for (int i = 0; i < SIDES; i++) {
            uint32_t b = (uint32_t)verts.size();
            float a0 = (float)i/SIDES*6.2832f, a1 = (float)(i+1)/SIDES*6.2832f;
            glm::vec2 uv0{cosf(a0)*0.5f+0.5f, sinf(a0)*0.5f+0.5f};
            glm::vec2 uv1{cosf(a1)*0.5f+0.5f, sinf(a1)*0.5f+0.5f};
            verts.push_back({bot_pts[i+1], {0,-1,0}, white, uv1});
            verts.push_back({bot_pts[i],   {0,-1,0}, white, uv0});
            idxs.insert(idxs.end(), {ci, b, b+1});
        }
    }

    m_vb = VulkanBuffer::createDeviceLocal(ctx, verts.data(), verts.size()*sizeof(Vertex3D),
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_ib = VulkanBuffer::createDeviceLocal(ctx, idxs.data(), idxs.size()*sizeof(uint32_t),
                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m_idxCount = (uint32_t)idxs.size();
}

void CursedObjects::destroy(VkDevice dev) {
    m_vb.destroy(dev); m_ib.destroy(dev);
}

bool CursedObjects::update(float dt, const glm::vec3& playerPos, const DayNight& time,
                            bool diamondPulseActive) {
    bool changed = false;
    m_playerNear = false;
    bool night = time.isNight();

    for (auto& o : m_objects) {
        if (o.found) { o.visible = false; continue; }

        bool shouldShow = o.nightOnly ? night : !night;
        // Diamond pulse reveals EVERYTHING regardless of time
        if (diamondPulseActive) shouldShow = true;

        if (shouldShow != o.visible) { o.visible = shouldShow; changed = true; }

        o.glitchPhase += dt * 2.3f;
        o.bobPhase    += dt * 1.1f;

        if (o.visible) {
            float dx = playerPos.x - o.pos.x;
            float dz = playerPos.z - o.pos.z;
            if (dx*dx + dz*dz <= CursedObject::INTERACT_R * CursedObject::INTERACT_R)
                m_playerNear = true;
        }
    }
    return changed;
}

int CursedObjects::tryInteract(const glm::vec3& playerPos) {
    float bestD = CursedObject::INTERACT_R * CursedObject::INTERACT_R;
    int   bestId = -1;
    for (auto& o : m_objects) {
        if (!o.visible || o.found) continue;
        float dx=playerPos.x-o.pos.x, dz=playerPos.z-o.pos.z;
        float d2=dx*dx+dz*dz;
        if (d2 < bestD) { bestD=d2; bestId=o.id; }
    }
    if (bestId < 0) return -1;

    m_objects[bestId].found = true;
    int total = foundCount();
    if (onFound) onFound(bestId, total >= 5);
    return bestId;
}

int CursedObjects::foundCount() const {
    int c=0; for (auto& o:m_objects) if(o.found) c++; return c;
}

void CursedObjects::setTexture(VkDescriptorSet ds) { m_texSet = ds; }

void CursedObjects::draw(VulkanRenderer& renderer, bool shadow) {
    if (!m_vb.buffer) return;
    for (auto& o : m_objects) {
        if (!o.visible) continue;
        // Slow bob + continuous rotation — nothing else in the world does this combo,
        // so the player will notice "something is off" without it being obvious what.
        float bob = sinf(o.bobPhase) * 0.10f;
        glm::mat4 world = glm::translate(glm::mat4(1.0f), o.pos + glm::vec3(0.f, bob, 0.f))
                        * glm::rotate(glm::mat4(1.0f), o.glitchPhase * 0.18f, glm::vec3(0.f,1.f,0.f));
        if (!shadow && m_texSet != VK_NULL_HANDLE)
            renderer.drawMeshTextured(m_vb.buffer, m_ib.buffer, m_idxCount, world, m_texSet, false);
        else
            renderer.drawMesh(m_vb.buffer, m_ib.buffer, m_idxCount, world, shadow);
    }
}
