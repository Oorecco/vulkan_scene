// scene/Scene.cpp — World geometry construction and per-frame update.
#include "Scene.h"
#include <vector>
#include <cmath>

// ── Frustum culling (column-major row-vector convention for GLM) ──────────
// GLM stores matrices column-major; clip = pos * VP in row-vector style.
// Extract planes from columns: left=col3+col0, right=col3-col0, etc.
static Frustum buildFrustum(const glm::mat4& vp) {
    Frustum f;
    glm::mat4 m = glm::transpose(vp); // transpose so rows = original columns
    f.planes[0] = m[3] + m[0]; // left
    f.planes[1] = m[3] - m[0]; // right
    f.planes[2] = m[3] + m[1]; // bottom
    f.planes[3] = m[3] - m[1]; // top
    f.planes[4] = m[3] + m[2]; // near
    f.planes[5] = m[3] - m[2]; // far
    for (auto& p : f.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 1e-5f) p /= len;
    }
    return f;
}

bool Scene::sphereVisible(const glm::vec3& c, float r) const {
    // Near-object guard: always render within 5m+radius regardless of frustum
    for (auto& p : frustum.planes) {
        float d = p.x*c.x + p.y*c.y + p.z*c.z + p.w;
        if (d < -r) return false;
    }
    return true;
}

void Scene::tryDraw(VulkanRenderer& renderer, Mesh& mesh, bool shadow) {
    if (!sphereVisible(mesh.bsCenter, mesh.bsRadius)) return;
    renderer.drawMesh(mesh.vb.buffer, mesh.ib.buffer,
                      mesh.idxCount, mesh.world, shadow);
}

// ── Static mesh helpers ───────────────────────────────────────────────────
static Mesh makeMesh(const VulkanContext& ctx,
                     const std::vector<Vertex3D>& verts,
                     const std::vector<uint32_t>& idxs,
                     glm::vec3 bsCenter, float bsRadius)
{
    Mesh m;
    m.vb = VulkanBuffer::createDeviceLocal(ctx,
        verts.data(), verts.size() * sizeof(Vertex3D),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m.ib = VulkanBuffer::createDeviceLocal(ctx,
        idxs.data(), idxs.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m.idxCount = (uint32_t)idxs.size();
    m.bsCenter = bsCenter;
    m.bsRadius = bsRadius;
    return m;
}

// Build a box with per-face colors and outward normals
static Mesh buildBox(const VulkanContext& ctx,
                     float hs, // half-size
                     const glm::vec3 faceColors[6],
                     glm::vec3 bsCenter, float bsRadius)
{
    // 6 faces × 4 verts = 24 verts
    // Using explicit float literals throughout because integer literals in
    // glm::vec3 initialisation cause MSVC to emit walls of template errors.
    // Lesson learned. The hard way.
    static const glm::vec3 NORMALS[6] = {
        {0.f,0.f,1.f},{0.f,0.f,-1.f},{-1.f,0.f,0.f},{1.f,0.f,0.f},{0.f,1.f,0.f},{0.f,-1.f,0.f}
    };
    static const glm::vec3 POSITIONS[6][4] = {
        {{-hs,-hs,+hs},{+hs,-hs,+hs},{+hs,+hs,+hs},{-hs,+hs,+hs}}, // front
        {{+hs,-hs,-hs},{-hs,-hs,-hs},{-hs,+hs,-hs},{+hs,+hs,-hs}}, // back
        {{-hs,-hs,-hs},{-hs,-hs,+hs},{-hs,+hs,+hs},{-hs,+hs,-hs}}, // left
        {{+hs,-hs,+hs},{+hs,-hs,-hs},{+hs,+hs,-hs},{+hs,+hs,+hs}}, // right
        {{-hs,+hs,+hs},{+hs,+hs,+hs},{+hs,+hs,-hs},{-hs,+hs,-hs}}, // top
        {{-hs,-hs,-hs},{+hs,-hs,-hs},{+hs,-hs,+hs},{-hs,-hs,+hs}}, // bottom
    };
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> idxs;
    verts.reserve(24); idxs.reserve(36);
    for (int f = 0; f < 6; ++f) {
        uint32_t base = (uint32_t)verts.size();
        for (int v = 0; v < 4; ++v)
            verts.push_back({ POSITIONS[f][v], NORMALS[f], faceColors[f], {0.f,0.f} });
        idxs.insert(idxs.end(), {base,base+1,base+2,base,base+2,base+3});
    }
    return makeMesh(ctx, verts, idxs, bsCenter, bsRadius);
}

// ── Mesh builders ─────────────────────────────────────────────────────────
Mesh Scene::buildColorCube(const VulkanContext& ctx) {
    glm::vec3 colors[6] = {
        {1.0f,.22f,.18f},{1.0f,.55f,.12f},{.20f,.85f,.35f},
        {.20f,.72f,.88f},{.30f,.45f,1.0f},{1.0f,.88f,.18f}
    };
    return buildBox(ctx, 0.5f, colors, glm::vec3(0.f,1.f,0.f), 0.9f);
}
Mesh Scene::buildOrangeCube(const VulkanContext& ctx) {
    glm::vec3 colors[6];
    for (auto& c : colors) c = {0.90f,0.48f,0.12f};
    return buildBox(ctx, PC_HALF, colors, {3.5f,PC_HALF,1.0f}, 0.95f);
}
Mesh Scene::buildGround(const VulkanContext& ctx) {
    float h = GROUND_HALF;
    float t = h / 4.0f; // UV tiling: one tile every 4m — looks natural at 440m scale
    glm::vec3 col = {1.0f,1.0f,1.0f}; // white so grass texture shows full color
    std::vector<Vertex3D> v = {
        {{-h,0.f,+h},{0.f,1.f,0.f},col,{0.f,t}},{{+h,0.f,+h},{0.f,1.f,0.f},col,{t,t}},
        {{-h,0.f,-h},{0.f,1.f,0.f},col,{0.f,0.f}},{{+h,0.f,-h},{0.f,1.f,0.f},col,{t,0.f}}
    };
    std::vector<uint32_t> i = {0,1,2,1,3,2};
    return makeMesh(ctx, v, i, glm::vec3(0.f), GROUND_HALF*2.0f);
}

Mesh Scene::buildCapsule(const VulkanContext& ctx) {
    const int SL=24, ST=10;
    const float R=CAP_RADIUS, CH=CAP_CYL_HALF;
    glm::vec3 CC = {0.35f,0.50f,0.78f};
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> idxs;

    auto addV = [&](float x,float y,float z,float nx,float ny,float nz){
        verts.push_back({{x,y,z},{nx,ny,nz},CC,{0.f,0.f}});
    };

    // Top hemisphere (fan rows)
    uint32_t topBase = 0;
    for (int j=0;j<ST;j++){
        float phi=(float)(j+1)/ST*(glm::pi<float>()*0.5f);
        float sp=sinf(phi),cp=cosf(phi);
        for (int i=0;i<=SL;i++){
            float th=(float)i/SL*glm::two_pi<float>(),ct=cosf(th),st=sinf(th);
            addV(R*cp*ct,CH+R*sp,R*cp*st, cp*ct,sp,cp*st);
        }
    }
    // Top ring + pole
    uint32_t topRingBase=(uint32_t)verts.size();
    for (int i=0;i<=SL;i++){float th=(float)i/SL*glm::two_pi<float>();addV(R*cosf(th),CH,R*sinf(th),cosf(th),0,sinf(th));}
    uint32_t topPole=(uint32_t)verts.size();addV(0,CH+R,0,0,1,0);
    for (int i=0;i<SL;i++) idxs.insert(idxs.end(),{topRingBase+i,topRingBase+(uint32_t)(i+1),topPole});
    for (int j=0;j<ST-1;j++)for (int i=0;i<SL;i++){
        uint32_t a=topBase+j*(SL+1)+i,b=a+1,c=topBase+(j+1)*(SL+1)+i,d=c+1;
        idxs.insert(idxs.end(),{a,b,c,b,d,c});
    }
    uint32_t topLast=topBase+(ST-1)*(SL+1);
    for (int i=0;i<SL;i++) idxs.insert(idxs.end(),{topLast+i,topPole,topLast+(uint32_t)(i+1)});

    // Cylinder
    uint32_t cylBase=(uint32_t)verts.size();
    for (int j=0;j<=1;j++) for (int i=0;i<=SL;i++){
        float y=j==0?CH:-CH,th=(float)i/SL*glm::two_pi<float>();
        addV(R*cosf(th),y,R*sinf(th),cosf(th),0,sinf(th));
    }
    for (int i=0;i<SL;i++){
        uint32_t a=cylBase+i,b=a+1,c=cylBase+(SL+1)+i,d=c+1;
        idxs.insert(idxs.end(),{a,b,c,b,d,c});
    }

    // Bottom hemisphere
    uint32_t botBase=(uint32_t)verts.size();
    for (int j=0;j<ST;j++){
        float phi=(float)(j+1)/ST*(glm::pi<float>()*0.5f),sp=sinf(phi),cp=cosf(phi);
        for (int i=0;i<=SL;i++){float th=(float)i/SL*glm::two_pi<float>(),ct=cosf(th),st=sinf(th);addV(R*cp*ct,-CH-R*sp,R*cp*st,cp*ct,-sp,cp*st);}
    }
    uint32_t botRingBase=(uint32_t)verts.size();
    for (int i=0;i<=SL;i++){float th=(float)i/SL*glm::two_pi<float>();addV(R*cosf(th),-CH,R*sinf(th),cosf(th),0,sinf(th));}
    uint32_t botPole=(uint32_t)verts.size();addV(0,-CH-R,0,0,-1,0);
    for (int i=0;i<SL;i++) idxs.insert(idxs.end(),{botRingBase+i,botRingBase+(uint32_t)(i+1),botPole});
    for (int j=0;j<ST-1;j++)for (int i=0;i<SL;i++){
        uint32_t a=botBase+j*(SL+1)+i,b=a+1,c=botBase+(j+1)*(SL+1)+i,d=c+1;
        idxs.insert(idxs.end(),{a,c,b,b,c,d});
    }
    uint32_t botLast=botBase+(ST-1)*(SL+1);
    for (int i=0;i<SL;i++) idxs.insert(idxs.end(),{botLast+i,botLast+(uint32_t)(i+1),botPole});

    return makeMesh(ctx,verts,idxs,SPAWN_POS,CAP_HALF_H+0.1f);
}

// ── Init / destroy ────────────────────────────────────────────────────────
void Scene::init(const VulkanContext& ctx) {
    m_displayCube = buildColorCube(ctx);
    m_physicsCube = buildOrangeCube(ctx);
    m_ground      = buildGround(ctx);
    m_capsule     = buildCapsule(ctx);
}

void Scene::destroy(VkDevice dev) {
    m_displayCube.destroy(dev);
    m_physicsCube.destroy(dev);
    m_ground.destroy(dev);
    m_capsule.destroy(dev);
}

// ── Per-frame update ──────────────────────────────────────────────────────
void Scene::update(float simTime, const Player& player,
                   const PhysicsCube& cube, bool freecamActive)
{
    // Display cube: rotates with sim time (freezes when paused)
    m_displayCube.world = glm::rotate(glm::mat4(1.0f),
        simTime * 0.8f, glm::vec3(0.f,1.f,0.f))
        * glm::translate(glm::mat4(1.0f), glm::vec3(0.f,1.f,0.f));
    m_displayCube.bsCenter = glm::vec3(m_displayCube.world[3]);

    // Physics cube: world from quaternion + position
    m_physicsCube.world = cube.worldMatrix();
    m_physicsCube.bsCenter = cube.pos;

    // Capsule follows player
    m_capsule.world = glm::translate(glm::mat4(1.0f), player.pos);
    m_capsule.bsCenter = player.pos;
}

// ── Draw ──────────────────────────────────────────────────────────────────
void Scene::draw(VulkanRenderer& r, bool shadow) {
    tryDraw(r, m_displayCube, shadow);
    // Ground: use tiled grass texture when available, plain white otherwise
    if (!shadow && m_groundTexSet != VK_NULL_HANDLE) {
        if (sphereVisible(m_ground.bsCenter, m_ground.bsRadius))
            r.drawMeshTextured(m_ground.vb.buffer, m_ground.ib.buffer,
                               m_ground.idxCount, m_ground.world,
                               m_groundTexSet, false);
    } else {
        tryDraw(r, m_ground, shadow);
    }
}
void Scene::drawCube(VulkanRenderer& r, bool shadow) {
    tryDraw(r, m_physicsCube, shadow);
}
void Scene::drawCapsule(VulkanRenderer& r, bool shadow) {
    tryDraw(r, m_capsule, shadow);
}
