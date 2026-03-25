#pragma once
// VulkanRenderer.h — The conductor. Orchestrates shadow pass, scene pass, UI.
// All the per-frame recording lives here.

#include "../Common.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"

// Forward declarations
class UIBatch;
class Scene;

// Per-frame GPU resources (one set per frame in flight)
struct FrameData {
    VkCommandBuffer  cmd          = VK_NULL_HANDLE;
    VkSemaphore      imageAvail   = VK_NULL_HANDLE;
    VkSemaphore      renderDone   = VK_NULL_HANDLE;
    VkFence          inFlight     = VK_NULL_HANDLE;
    VulkanBuffer     ubo;         // per-frame UBO
    VkDescriptorSet  sceneSet     = VK_NULL_HANDLE;
    VkDescriptorSet  uiSet        = VK_NULL_HANDLE;
    VulkanBuffer     uiVB;        // dynamic UI vertex buffer
};

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();

    // Initialise renderer — call once after window creation
    // vulkanVersionPref: 0=1.3, 1=1.2, 2=1.1
    void init(HWND hwnd, uint32_t w, uint32_t h, bool vsync, int vulkanVersionPref);

    // Resize event
    void resize(uint32_t w, uint32_t h);

    // Begin a frame. Returns false if the swapchain is out-of-date.
    bool beginFrame();

    // Record scene geometry into the current command buffer.
    // shadow = true: rendering into shadow map. false: main pass.
    void drawMesh(VkBuffer vb, VkBuffer ib, uint32_t idxCount,
                  const glm::mat4& model, bool shadow = false);

    // Textured variant — binds diffuseSet as set 1 before drawing.
    // Use allocateDiffuseSet() to create one per unique texture.
    void drawMeshTextured(VkBuffer vb, VkBuffer ib, uint32_t idxCount,
                          const glm::mat4& model,
                          VkDescriptorSet  diffuseSet,
                          bool shadow = false);

    // Create a diffuse descriptor set bound to a texture image.
    // Returns VK_NULL_HANDLE on failure. Caller owns the set lifecycle.
    VkDescriptorSet allocateDiffuseSet(VkImageView view, VkSampler sampler) const;

    // Submit a UI vertex batch (called once per frame after all scene drawing)
    void submitUI(UIBatch& batch);

    // End frame and present
    void endFrame();

    // Update per-frame UBO
    void updateFrameUBO(const UBOFrame& ubo);

    // Destroy everything
    void shutdown();

    // ── Accessors for the game scene ──────────────────────────────────────
    VulkanContext&  context()   { return m_ctx; }
    VkExtent2D      extent()    { return m_swapchain.extent(); }
    uint32_t        width()     { return m_swapchain.extent().width; }
    uint32_t        height()    { return m_swapchain.extent().height; }
    VkSampler       shadowSampler() const { return m_shadowSampler; }
    VkSampler       fontSampler()   const { return m_fontSampler; }

    // Font atlas image (created externally, registered here)
    void registerFontAtlas(VkImageView view, VkSampler sampler);

    // White 1x1 fallback diffuse set — bound for untextured meshes so
    // the shader's diffuse multiply doesn't need a special code path
    VkDescriptorSet whiteDiffuseSet() const { return m_whiteDiffuseSet; }

    bool vsync()         const { return m_vsync; }
    void setVSync(bool v)      { m_vsync = v; m_needsResize = true; }

    // Sky clear color — updated every frame from DayNight system.
    // Previously hardcoded to a boring grey. Now it matches the actual sky.
    void setClearColor(const glm::vec3& sky) {
        m_clearColor = { sky.x, sky.y, sky.z, 1.0f };
    }
    // Update sky clear color dynamically from DayNight each frame
    void setSkyColor(float r, float g, float b) { m_skyR=r; m_skyG=g; m_skyB=b; }

    static const uint32_t UI_MAX_VERTS = 26000;

private:
    float m_skyR = 0.20f, m_skyG = 0.20f, m_skyB = 0.22f; // dynamic sky clear color
    void createSamplers();
    void createFrameData();
    void destroyFrameData();
    void recreateSwapchain();
    void beginShadowPass();
    void endShadowPass();
    void beginMainPass();
    void endMainPass();

    VulkanContext   m_ctx;
    VulkanSwapchain m_swapchain;
    VulkanPipeline  m_pipeline;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    uint32_t  m_currentFrame = 0;
    uint32_t  m_imageIndex   = 0;
    bool      m_inFrame      = false;
    bool      m_vsync        = true;
    bool      m_needsResize  = false;
    glm::vec4 m_clearColor   = { 0.20f, 0.20f, 0.22f, 1.0f };
    uint32_t  m_pendingW     = 0;
    uint32_t  m_pendingH     = 0;

    VkSampler m_shadowSampler    = VK_NULL_HANDLE;
    VkSampler m_fontSampler      = VK_NULL_HANDLE;

    VulkanImage     m_whiteImage;           // 1x1 white texture for untextured draws
    VkDescriptorSet m_whiteDiffuseSet = VK_NULL_HANDLE;

    // Font atlas (registered by game after UIBatch init)
    VkImageView m_fontView    = VK_NULL_HANDLE;
    VkSampler   m_fontSampReg = VK_NULL_HANDLE;

    // Shadow pass state
    bool m_shadowPassActive = false;
    bool m_mainPassActive   = false;
};
