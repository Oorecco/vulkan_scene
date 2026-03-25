#pragma once
// VulkanPipeline.h — Descriptor layouts, render passes, and all pipeline objects.
// One file to rule the pipeline state. Shader loading is also here.

#include "../Common.h"
#include "VulkanContext.h"
#include "VulkanImage.h"
#include <vector>
#include <string>

// Shadow map resolution
constexpr uint32_t SHADOW_DIM = 2048;

// UI push constant for mode selection
struct UIPushConst {
    int mode; // 0=solid 1=text
};

class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    // Compile shaders and build all pipelines + render passes
    // shaderDir: path to folder containing .vert/.frag GLSL files
    void create(const VulkanContext& ctx,
                VkExtent2D          swapExtent,
                VkFormat            swapFormat,
                const std::string&  shaderDir,
                uint32_t            shadowDim = SHADOW_DIM);

    void destroy(VkDevice device);

    // Resize: rebuild pipelines that depend on swapchain extent
    void resize(VkDevice device, VkExtent2D newExtent, VkFormat swapFormat);

    // ── Descriptor set management ─────────────────────────────────────────
    // Creates a per-frame descriptor set bound to the given UBO and shadow map
    VkDescriptorSet allocateSceneSet(VkDevice device,
                                     VkBuffer uboBuffer,
                                     VkDeviceSize uboSize,
                                     const VulkanImage& shadowMap,
                                     VkSampler shadowSampler) const;

    VkDescriptorSet allocateUISet(VkDevice device,
                                  VkImageView fontView,
                                  VkSampler   fontSampler) const;

    // Allocate a per-draw diffuse texture descriptor (set 1)
    VkDescriptorSet allocateDiffuseSet(VkDevice device,
                                       VkImageView diffuseView,
                                       VkSampler   sampler) const;

    // ── Accessors ─────────────────────────────────────────────────────────
    VkRenderPass   mainRenderPass()   const { return m_mainRenderPass; }
    VkRenderPass   shadowRenderPass() const { return m_shadowRenderPass; }
    VkPipeline     scenePipeline()    const { return m_scenePipeline; }
    VkPipeline     shadowPipeline()   const { return m_shadowPipeline; }
    VkPipeline     uiPipeline()       const { return m_uiPipeline; }
    VkPipelineLayout sceneLayout()    const { return m_sceneLayout; }
    VkPipelineLayout uiLayout()       const { return m_uiLayout; }
    VkDescriptorSetLayout sceneDescLayout()   const { return m_sceneDescLayout; }
    VkDescriptorSetLayout uiDescLayout()      const { return m_uiDescLayout; }
    VkDescriptorSetLayout diffuseDescLayout() const { return m_diffuseDescLayout; }
    VkDescriptorPool descPool()       const { return m_descPool; }
    uint32_t shadowDim()              const { return m_shadowDim; }

    // Shadow map framebuffer (for shadow pass)
    VkFramebuffer  shadowFramebuffer() const { return m_shadowFB; }
    VulkanImage&   shadowDepth()       { return m_shadowDepth; }

private:
    std::vector<uint32_t> compileSPIRV(const std::string& glslPath,
                                        VkShaderStageFlagBits stage) const;
    VkShaderModule        createModule(VkDevice device,
                                       const std::vector<uint32_t>& spv) const;

    void createRenderPasses(VkDevice device, VkFormat swapFmt);
    void createDescriptorLayouts(VkDevice device);
    void createDescriptorPool(VkDevice device);
    void createScenePipeline(VkDevice device, VkExtent2D ext,
                              const std::string& shaderDir);
    void createShadowPipeline(VkDevice device,
                              const std::string& shaderDir);
    void createUIPipeline(VkDevice device, VkExtent2D ext,
                          const std::string& shaderDir);
    void createShadowFramebuffer(const VulkanContext& ctx);

    VkRenderPass   m_mainRenderPass   = VK_NULL_HANDLE;
    VkRenderPass   m_shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_sceneLayout    = VK_NULL_HANDLE;
    VkPipelineLayout m_uiLayout       = VK_NULL_HANDLE;
    VkPipeline     m_scenePipeline    = VK_NULL_HANDLE;
    VkPipeline     m_shadowPipeline   = VK_NULL_HANDLE;
    VkPipeline     m_uiPipeline       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_sceneDescLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_uiDescLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_diffuseDescLayout = VK_NULL_HANDLE; // set 1: diffuse tex
    VkDescriptorPool      m_descPool        = VK_NULL_HANDLE;

    VulkanImage    m_shadowDepth;
    VkFramebuffer  m_shadowFB   = VK_NULL_HANDLE;
    uint32_t       m_shadowDim  = SHADOW_DIM;
    std::string    m_shaderDir;

    const VulkanContext* m_ctx = nullptr; // borrowed reference for resize
};
