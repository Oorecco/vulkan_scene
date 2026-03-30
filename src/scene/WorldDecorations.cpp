// WorldDecorations.cpp — Making the world less empty, one polygon at a time.
// All GLM constructors use explicit float literals throughout.
// "But {0,1,0} should work!" — narrator: it did not work. Not with aligned types.
// Now it does, because we learned our lesson and type everything out.
#include "WorldDecorations.h"
#include <cmath>
#include <algorithm>

static const glm::vec3 YAXIS{0.f,1.f,0.f}; // Y rotation axis — used everywhere

// ── Generic mesh builder helper ───────────────────────────────────────────
static PropMesh makePropMesh(const VulkanContext& ctx,
                              const std::vector<Vertex3D>& verts,
                              const std::vector<uint32_t>& idxs,
                              const std::string& tex,
                              bool doubleSided = false)
{
    PropMesh m;
    m.vb = VulkanBuffer::createDeviceLocal(ctx,
        verts.data(), verts.size()*sizeof(Vertex3D),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m.ib = VulkanBuffer::createDeviceLocal(ctx,
        idxs.data(), idxs.size()*sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m.idxCount = (uint32_t)idxs.size();
    m.textureName = tex;
    m.doubleSided = doubleSided;
    return m;
}

static PropMesh buildBox(const VulkanContext& ctx, glm::vec3 halfExt, const std::string& tex)
{
    float hx=halfExt.x, hy=halfExt.y, hz=halfExt.z;
    const glm::vec3 N[6] = {
        {0.f,0.f,1.f},{0.f,0.f,-1.f},{-1.f,0.f,0.f},
        {1.f,0.f,0.f},{0.f,1.f,0.f}, {0.f,-1.f,0.f}
    };
    const glm::vec3 P[6][4]={
        {{-hx,-hy,+hz},{+hx,-hy,+hz},{+hx,+hy,+hz},{-hx,+hy,+hz}},
        {{+hx,-hy,-hz},{-hx,-hy,-hz},{-hx,+hy,-hz},{+hx,+hy,-hz}},
        {{-hx,-hy,-hz},{-hx,-hy,+hz},{-hx,+hy,+hz},{-hx,+hy,-hz}},
        {{+hx,-hy,+hz},{+hx,-hy,-hz},{+hx,+hy,-hz},{+hx,+hy,+hz}},
        {{-hx,+hy,+hz},{+hx,+hy,+hz},{+hx,+hy,-hz},{-hx,+hy,-hz}},
        {{-hx,-hy,-hz},{+hx,-hy,-hz},{+hx,-hy,+hz},{-hx,-hy,+hz}},
    };
    // Per-face UV patterns — planar projection per axis
    const glm::vec2 UV[6][4]={
        {{0,1},{1,1},{1,0},{0,0}}, // +Z
        {{0,1},{1,1},{1,0},{0,0}}, // -Z
        {{0,1},{1,1},{1,0},{0,0}}, // -X
        {{0,1},{1,1},{1,0},{0,0}}, // +X
        {{0,0},{1,0},{1,1},{0,1}}, // top
        {{0,0},{1,0},{1,1},{0,1}}, // bottom
    };
    std::vector<Vertex3D> v; std::vector<uint32_t> idx;
    glm::vec3 white{1.f,1.f,1.f};
    for (int f=0;f<6;f++) {
        uint32_t b=(uint32_t)v.size();
        for (int i=0;i<4;i++) v.push_back({P[f][i], N[f], white, UV[f][i]});
        idx.insert(idx.end(),{b,b+1,b+2,b,b+2,b+3});
    }
    return makePropMesh(ctx,v,idx,tex);
}

// ── Mesh builders ─────────────────────────────────────────────────────────
PropMesh WorldDecorations::buildTreeTrunk(const VulkanContext& ctx) {
    return buildBox(ctx,{0.18f,1.2f,0.18f},"textures/props/bark.png");
}

PropMesh WorldDecorations::buildTreeCone(const VulkanContext& ctx) {
    const int SIDES=8; const float R=0.85f, H=1.8f;
    std::vector<Vertex3D> v; std::vector<uint32_t> idx;
    glm::vec3 col{1.f,1.f,1.f};
    uint32_t apex=(uint32_t)v.size();
    v.push_back({{0.f,H,0.f},{0.f,1.f,0.f},col,{0.5f,0.f}});
    uint32_t base=(uint32_t)v.size();
    for (int i=0;i<=SIDES;i++) {
        float a=(float)i/SIDES*6.2832f;
        glm::vec3 p={cosf(a)*R,0.f,sinf(a)*R};
        glm::vec3 n=glm::normalize(glm::vec3(cosf(a),0.4f,sinf(a)));
        v.push_back({p,n,col,{(float)i/SIDES,1.f}});
    }
    for (int i=0;i<SIDES;i++) {
        idx.push_back(apex); idx.push_back(base+i); idx.push_back(base+i+1);
    }
    uint32_t center=(uint32_t)v.size();
    v.push_back({{0.f,0.f,0.f},{0.f,-1.f,0.f},col,{0.5f,0.5f}});
    for (int i=0;i<SIDES;i++) {
        idx.push_back(center); idx.push_back(base+i+1); idx.push_back(base+i);
    }
    return makePropMesh(ctx,v,idx,"textures/props/leaves.png");
}

PropMesh WorldDecorations::buildRock(const VulkanContext& ctx, float scale) {
    const int LAT=6,LON=8;
    std::vector<Vertex3D> v; std::vector<uint32_t> idx;
    glm::vec3 col{1.f,1.f,1.f};
    unsigned int s=(unsigned int)(scale*1000);
    auto rjit=[&](){ s=s*1664525u+1013904223u; return (float)(s&0xFFFF)/65535.f*0.25f-0.125f; };
    for (int la=0;la<=LAT;la++) {
        float phi=(float)la/LAT*3.14159f;
        for (int lo=0;lo<=LON;lo++) {
            float th=(float)lo/LON*6.2832f;
            float r=scale*(0.9f+rjit());
            glm::vec3 p={r*sinf(phi)*cosf(th), r*cosf(phi)*0.7f, r*sinf(phi)*sinf(th)};
            glm::vec3 n=glm::normalize(p);
            glm::vec2 uv={(float)lo/LON, (float)la/LAT};
            v.push_back({p,n,col,uv});
        }
    }
    for (int la=0;la<LAT;la++)
        for (int lo=0;lo<LON;lo++) {
            uint32_t a=(uint32_t)(la*(LON+1)+lo), b=a+1, c=a+(LON+1), d=c+1;
            idx.insert(idx.end(),{a,b,c,b,d,c});
        }
    return makePropMesh(ctx,v,idx,"textures/props/rock.png");
}

PropMesh WorldDecorations::buildMushroom(const VulkanContext& ctx) {
    return buildBox(ctx,{0.06f,0.22f,0.06f},"textures/props/mushroom_stem.png");
}

PropMesh WorldDecorations::buildMushroomCap(const VulkanContext& ctx) {
    const int S=10; const float R=0.28f;
    std::vector<Vertex3D> v; std::vector<uint32_t> idx;
    glm::vec3 col{1.f,1.f,1.f}, up{0.f,1.f,0.f};
    uint32_t top=(uint32_t)v.size();
    v.push_back({{0.f,0.12f,0.f},up,col,{0.5f,0.5f}});
    uint32_t ring=(uint32_t)v.size();
    for (int i=0;i<=S;i++) {
        float a=(float)i/S*6.2832f;
        v.push_back({{cosf(a)*R,-0.04f,sinf(a)*R},up,col,
            {0.5f+cosf(a)*0.5f, 0.5f+sinf(a)*0.5f}});
    }
    for (int i=0;i<S;i++) { idx.push_back(top);idx.push_back(ring+i);idx.push_back(ring+i+1); }
    return makePropMesh(ctx,v,idx,"textures/props/mushroom_cap.png",true);
}

PropMesh WorldDecorations::buildCrateMesh(const VulkanContext& ctx) {
    return buildBox(ctx,{0.4f,0.4f,0.4f},"textures/props/crate.png");
}
PropMesh WorldDecorations::buildFencePost(const VulkanContext& ctx) {
    return buildBox(ctx,{0.06f,0.5f,0.06f},"textures/props/fence_wood.png");
}
PropMesh WorldDecorations::buildFenceRail(const VulkanContext& ctx) {
    return buildBox(ctx,{0.9f,0.04f,0.04f},"textures/props/fence_wood.png");
}

PropMesh WorldDecorations::buildGrassQuad(const VulkanContext& ctx) {
    const float W=0.25f, H=0.5f;
    std::vector<Vertex3D> v; std::vector<uint32_t> idx;
    glm::vec3 col{1.f,1.f,1.f}, n{0.f,1.f,0.f};
    v.push_back({{-W,0.f,0.f},n,col,{0.f,1.f}}); v.push_back({{+W,0.f,0.f},n,col,{1.f,1.f}});
    v.push_back({{+W,H, 0.f},n,col,{1.f,0.f}}); v.push_back({{-W,H, 0.f},n,col,{0.f,0.f}});
    idx.insert(idx.end(),{0,1,2,0,2,3});
    v.push_back({{0.f,0.f,-W},n,col,{0.f,1.f}}); v.push_back({{0.f,0.f,+W},n,col,{1.f,1.f}});
    v.push_back({{0.f,H, +W},n,col,{1.f,0.f}}); v.push_back({{0.f,H, -W},n,col,{0.f,0.f}});
    idx.insert(idx.end(),{4,5,6,4,6,7});
    return makePropMesh(ctx,v,idx,"textures/props/grass_2d.png",true);
}

PropMesh WorldDecorations::buildBush(const VulkanContext& ctx) {
    return buildBox(ctx,{0.3f,0.25f,0.3f},"textures/props/leaves.png");
}
PropMesh WorldDecorations::buildStone(const VulkanContext& ctx) { return buildRock(ctx,0.22f); }
PropMesh WorldDecorations::buildSignPost(const VulkanContext& ctx) {
    return buildBox(ctx,{0.28f,0.22f,0.03f},"textures/props/sign_wood.png");
}

// ── Init / destroy ────────────────────────────────────────────────────────
void WorldDecorations::init(const VulkanContext& ctx, VulkanRenderer& renderer,
                             AssetManager& assets, unsigned int seed) {
    m_renderer = &renderer;
    m_byType.resize((int)PropType::COUNT);
    m_meshes[(int)PropType::Tree]          = buildTreeTrunk(ctx);
    m_meshes[(int)PropType::Rock]          = buildRock(ctx, 0.45f);
    m_meshes[(int)PropType::Mushroom]      = buildMushroom(ctx);
    m_meshes[(int)PropType::Crate]         = buildCrateMesh(ctx);
    m_meshes[(int)PropType::FencePost]     = buildFencePost(ctx);
    m_meshes[(int)PropType::FenceRail]     = buildFenceRail(ctx);
    m_meshes[(int)PropType::GrassBillboard]= buildGrassQuad(ctx);
    m_meshes[(int)PropType::Bush]          = buildTreeCone(ctx); // Bush slot = tree canopy (cone)
    m_meshes[(int)PropType::Stone]         = buildMushroomCap(ctx); // Stone slot = mushroom cap
    m_meshes[(int)PropType::Sign]          = buildSignPost(ctx);

    // Pre-allocate a texture descriptor set for every unique texture path used.
    // One set per texture — drawn meshes just look it up by name. Fast and simple.
    auto buildSet = [&](const std::string& path) {
        if (m_texSets.count(path)) return;
        const auto& entry = assets.getTexture(path);
        VkDescriptorSet set = renderer.allocateDiffuseSet(
            entry.image.view,
            entry.sampler != VK_NULL_HANDLE ? entry.sampler : assets.repeatSampler());
        m_texSets[path] = set;
    };
    for (int i = 0; i < (int)PropType::COUNT; i++) {
        if (!m_meshes[i].textureName.empty())
            buildSet(m_meshes[i].textureName);
    }

    placeProps(seed);
}
void WorldDecorations::destroy(VkDevice dev) {
    for (int i=0;i<(int)PropType::COUNT;i++) m_meshes[i].destroy(dev);
}

// ── Placement ─────────────────────────────────────────────────────────────
void WorldDecorations::placeProps(unsigned int seed) {
    unsigned int rng = seed;
    // Scale for 440x440m world. Clusters spread across the full area, not bunched at origin.
    // Tree clusters: ~20 clusters spread around the world, 6-14 trees each
    float half = GROUND_HALF * 0.85f; // keep clusters away from the very edge
    for (int c = 0; c < 20; c++) {
        float cx = (lcgF(rng)*2.f-1.f)*half;
        float cz = (lcgF(rng)*2.f-1.f)*half;
        // Keep a clear area around spawn (origin)
        if (std::abs(cx)<15.f && std::abs(cz)<15.f) { cx += (cx>=0?20.f:-20.f); }
        int cnt = 6 + (int)(lcgF(rng)*8.f); // 6-14 trees per cluster
        placeTreeCluster(rng, {cx,cz}, cnt);
    }
    placeRockScatter(rng, 180);   // 180 rocks across 440m world
    placeMushrooms(rng, 220);     // mushrooms scattered everywhere
    placeGrassFields(rng, 1200);  // dense grass, but 440m needs a lot
    placeFences(rng);
    placeSigns(rng, 40);          // signs peppered around
    for (auto& g : m_byType)
        for (auto& inst : g)
            m_allInstances.push_back(inst);
}

void WorldDecorations::placeTreeCluster(unsigned int& rng, glm::vec2 center, int count) {
    for (int i=0;i<count;i++) {
        float ang   = lcgF(rng)*6.2832f;
        float dist  = 3.0f + lcgF(rng)*15.0f; // 3-18m spread — prevents overlap
        float x     = std::clamp(center.x+cosf(ang)*dist,-GROUND_HALF+1.5f,GROUND_HALF-1.5f);
        float z     = std::clamp(center.y+sinf(ang)*dist,-GROUND_HALF+1.5f,GROUND_HALF-1.5f);
        float scale = 0.8f + lcgF(rng)*0.6f;
        float yaw   = lcgF(rng)*6.2832f;

        PropInstance trunk;
        trunk.pos = {x, GROUND_Y+1.2f*scale, z};
        trunk.yaw = yaw; trunk.scale = scale;
        trunk.world = glm::translate(glm::mat4(1.0f), glm::vec3(x,GROUND_Y,z))
                    * glm::rotate(glm::mat4(1.0f), yaw, YAXIS)
                    * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        trunk.bsCenter = trunk.pos; trunk.bsRadius = 0.25f*scale;
        trunk.hasCollision = true;
        trunk.aabbMin = {x-0.2f*scale, GROUND_Y,            z-0.2f*scale};
        trunk.aabbMax = {x+0.2f*scale, GROUND_Y+2.4f*scale, z+0.2f*scale};
        m_byType[(int)PropType::Tree].push_back(trunk);

        PropInstance canopy;
        float coneY = GROUND_Y+2.4f*scale;
        canopy.pos = {x,coneY,z};
        canopy.world = glm::translate(glm::mat4(1.0f), glm::vec3(x,coneY,z))
                     * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        canopy.bsCenter = {x, coneY+0.9f*scale, z};
        canopy.bsRadius = 1.0f*scale;
        canopy.hasCollision = false;
        m_byType[(int)PropType::Bush].push_back(canopy); // canopy uses Bush slot (cone mesh)
    }
}

void WorldDecorations::placeRockScatter(unsigned int& rng, int count) {
    for (int i=0;i<count;i++) {
        auto pos    = randPosOnGround(rng, 3.0f, GROUND_HALF-2.0f);
        float scale = 0.3f + lcgF(rng)*0.5f;
        float yaw   = lcgF(rng)*6.2832f;
        PropInstance inst;
        inst.pos = pos; inst.yaw = yaw; inst.scale = scale;
        inst.world = glm::translate(glm::mat4(1.0f), pos)
                   * glm::rotate(glm::mat4(1.0f), yaw, YAXIS)
                   * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        inst.bsCenter = pos; inst.bsRadius = scale*0.55f;
        inst.hasCollision = (scale > 0.35f);
        inst.aabbMin = pos - glm::vec3(scale*0.5f);
        inst.aabbMax = pos + glm::vec3(scale*0.5f);
        m_byType[(int)PropType::Rock].push_back(inst);
    }
}

void WorldDecorations::placeMushrooms(unsigned int& rng, int count) {
    for (int i=0;i<count;i++) {
        auto pos    = randPosOnGround(rng, 2.0f, GROUND_HALF-2.0f);
        float scale = 0.6f + lcgF(rng)*0.8f;
        PropInstance stem;
        stem.pos = pos; stem.scale = scale;
        stem.world = glm::translate(glm::mat4(1.0f), pos)
                   * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        stem.bsCenter = pos; stem.bsRadius = 0.15f*scale;
        stem.hasCollision = false; stem.aabbMin = pos; stem.aabbMax = pos;
        m_byType[(int)PropType::Mushroom].push_back(stem);
        PropInstance cap = stem;
        cap.pos.y = pos.y+0.22f*scale;
        cap.world = glm::translate(glm::mat4(1.0f), cap.pos)
                  * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        m_byType[(int)PropType::Stone].push_back(cap);
    }
}

void WorldDecorations::placeGrassFields(unsigned int& rng, int count) {
    for (int i=0;i<count;i++) {
        auto pos  = randPosOnGround(rng, 1.0f, GROUND_HALF-1.0f);
        float yaw = lcgF(rng)*6.2832f;
        PropInstance inst;
        inst.pos = pos; inst.yaw = yaw; inst.scale = 1.0f;
        inst.world = glm::translate(glm::mat4(1.0f), pos)
                   * glm::rotate(glm::mat4(1.0f), yaw, YAXIS);
        inst.bsCenter = pos; inst.bsRadius = 0.35f;
        inst.hasCollision = false;
        m_byType[(int)PropType::GrassBillboard].push_back(inst);
    }
}

void WorldDecorations::placeFences(unsigned int& rng) {
    // Scatter ~15 short fence segments across the world at random angles.
    // Each segment has 6-12 posts with connecting rails.
    for (int seg = 0; seg < 15; seg++) {
        float cx  = (lcgF(rng)*2.f-1.f)*GROUND_HALF*0.75f;
        float cz  = (lcgF(rng)*2.f-1.f)*GROUND_HALF*0.75f;
        if (std::abs(cx)<8.f && std::abs(cz)<8.f) continue; // clear spawn area
        float ang = lcgF(rng)*6.2832f; // fence line angle
        float dx  = cosf(ang)*1.8f, dz = sinf(ang)*1.8f;
        int   num = 6 + (int)(lcgF(rng)*7.f); // 6-12 posts per segment
        std::vector<glm::vec3> postPositions;
        postPositions.reserve(num);
        for (int i = 0; i < num; i++) {
            float x = cx + dx*i, z = cz + dz*i;
            if (std::abs(x)>GROUND_HALF-1.f || std::abs(z)>GROUND_HALF-1.f) break;
            if (lcgF(rng)<0.12f) continue; // occasional gap for variety
            PropInstance post;
            post.pos = {x, GROUND_Y+0.5f, z};
            post.world = glm::translate(glm::mat4(1.0f), post.pos);
            post.bsCenter = post.pos; post.bsRadius = 0.12f;
            post.hasCollision = true;
            post.aabbMin = post.pos - glm::vec3(0.07f,0.5f,0.07f);
            post.aabbMax = post.pos + glm::vec3(0.07f,0.5f,0.07f);
            m_byType[(int)PropType::FencePost].push_back(post);
            postPositions.push_back(post.pos);
        }

        // Connect only actually placed posts so rails align with real pillars.
        for (size_t i = 0; i + 1 < postPositions.size(); i++) {
            glm::vec3 a = postPositions[i];
            glm::vec3 b = postPositions[i + 1];
            glm::vec3 d = b - a;
            float len = glm::length(glm::vec2(d.x, d.z));
            if (len < 1e-3f) continue;

            float yaw = std::atan2(d.z, d.x);
            float scaleX = std::min(1.0f, len / 1.8f);

            PropInstance rail;
            rail.pos = {(a.x + b.x) * 0.5f, GROUND_Y + 0.35f, (a.z + b.z) * 0.5f};
            rail.world = glm::translate(glm::mat4(1.0f), rail.pos)
                       * glm::rotate(glm::mat4(1.0f), yaw, YAXIS)
                       * glm::scale(glm::mat4(1.0f), glm::vec3(scaleX, 1.0f, 1.0f));
            rail.bsCenter = rail.pos;
            rail.bsRadius = std::max(0.3f, len * 0.5f);
            rail.hasCollision = false;
            m_byType[(int)PropType::FenceRail].push_back(rail);
        }
    }
}

void WorldDecorations::placeSigns(unsigned int& rng, int count) {
    for (int i=0;i<count;i++) {
        auto pos  = randPosOnGround(rng, 10.0f, GROUND_HALF-5.0f);
        pos.y = GROUND_Y+1.0f;
        float yaw = lcgF(rng)*6.2832f;
        PropInstance inst;
        inst.pos = pos; inst.yaw = yaw;
        inst.world = glm::translate(glm::mat4(1.0f), pos)
                   * glm::rotate(glm::mat4(1.0f), yaw, YAXIS);
        inst.bsCenter = pos; inst.bsRadius = 0.35f; inst.hasCollision = false;
        m_byType[(int)PropType::Sign].push_back(inst);
    }
}

// ── Frustum / draw / collision ────────────────────────────────────────────
bool WorldDecorations::visible(const Frustum& f, const glm::vec3& c, float r) const {
    for (auto& p : f.planes)
        if (p.x*c.x + p.y*c.y + p.z*c.z + p.w < -r) return false;
    return true;
}

void WorldDecorations::drawProp(VulkanRenderer& r, const PropMesh& mesh,
                                 const glm::mat4& world, bool shadow, bool) {
    VkDescriptorSet texSet = VK_NULL_HANDLE;
    if (!shadow && !mesh.textureName.empty()) {
        auto it = m_texSets.find(mesh.textureName);
        if (it != m_texSets.end()) texSet = it->second;
    }
    if (texSet != VK_NULL_HANDLE)
        r.drawMeshTextured(mesh.vb.buffer, mesh.ib.buffer, mesh.idxCount, world, texSet, shadow);
    else
        r.drawMesh(mesh.vb.buffer, mesh.ib.buffer, mesh.idxCount, world, shadow);
}

void WorldDecorations::draw(VulkanRenderer& renderer, const Frustum& frustum,
                             const glm::vec3& camEye, bool shadow)
{
    m_drawnCount = m_culledCount = 0;
    for (int t=0;t<(int)PropType::COUNT;t++) {
        const PropMesh& mesh = m_meshes[t];
        if (!mesh.vb.buffer) continue;
        for (auto& inst : m_byType[t]) {
            float dx=inst.bsCenter.x-camEye.x, dz=inst.bsCenter.z-camEye.z;
            float guard=inst.bsRadius+5.0f;
            if (dx*dx+dz*dz > guard*guard && !visible(frustum,inst.bsCenter,inst.bsRadius)) {
                m_culledCount++; continue;
            }
            drawProp(renderer, mesh, inst.world, shadow, mesh.doubleSided);
            m_drawnCount++;
        }
    }
}

void WorldDecorations::resolvePlayerCollision(glm::vec3& playerPos,
                                               float r, float halfH) const
{
    for (auto& inst : m_allInstances) {
        if (!inst.hasCollision) continue;
        float cx=std::clamp(playerPos.x,inst.aabbMin.x,inst.aabbMax.x);
        float cz=std::clamp(playerPos.z,inst.aabbMin.z,inst.aabbMax.z);
        float dx=playerPos.x-cx, dz=playerPos.z-cz, d2=dx*dx+dz*dz;
        if (d2<r*r && playerPos.y-halfH<inst.aabbMax.y && playerPos.y+halfH>inst.aabbMin.y) {
            float dist=sqrtf(d2);
            if (dist<0.001f){dx=1.f;dz=0.f;dist=1.f;}
            float push=r-dist+0.005f;
            playerPos.x+=(dx/dist)*push; playerPos.z+=(dz/dist)*push;
        }
    }
}
