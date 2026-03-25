// VulkanRenderer.cpp
#include "VulkanRenderer.h"
#include "../ui/UIBatch.h"
#include <stdexcept>

VulkanRenderer::~VulkanRenderer() { shutdown(); }

void VulkanRenderer::init(HWND hwnd, uint32_t w, uint32_t h,
                           bool vsync, int vulkanVersionPref)
{
    m_vsync    = vsync;
    m_pendingW = w;
    m_pendingH = h;

    m_ctx.init(hwnd, vulkanVersionPref);

    // Build pipelines (shaders compiled here — this is the loading screen work)
    std::string shaderDir = ExeDir() + "shaders";
    m_pipeline.create(m_ctx, { w, h },
        VK_FORMAT_B8G8R8A8_SRGB, // placeholder; swapchain will confirm
        shaderDir);

    m_swapchain.create(m_ctx, w, h, vsync, m_pipeline.mainRenderPass());

    createSamplers();
    createFrameData();

    // White 1x1 fallback — bound as set 1 for all untextured drawMesh() calls.
    // This lets the shader always sample diffuseTex without a special code path.
    // The multiply is: vertex_color * (1,1,1) = vertex_color. Maths: still correct.
    m_whiteImage       = VulkanImage::createWhite(m_ctx);
    m_whiteDiffuseSet  = m_pipeline.allocateDiffuseSet(
        m_ctx.device(), m_whiteImage.view, m_fontSampler);

    LOG_INFO("VulkanRenderer initialised.");
}

void VulkanRenderer::createSamplers() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_ctx.physDevice(), &props);

    // Shadow sampler with PCF support
    {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = VK_FILTER_LINEAR;
        ci.minFilter    = VK_FILTER_LINEAR;
        ci.compareEnable= VK_TRUE;
        ci.compareOp    = VK_COMPARE_OP_LESS_OR_EQUAL;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &ci, nullptr, &m_shadowSampler));
    }
    // Font sampler (point filtering — we want crisp pixel text)
    {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = VK_FILTER_NEAREST;
        ci.minFilter    = VK_FILTER_NEAREST;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &ci, nullptr, &m_fontSampler));
    }
}

void VulkanRenderer::createFrameData() {
    VkCommandBufferAllocateInfo cai{};
    cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool        = m_ctx.cmdPool();
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> cmds;
    VK_CHECK(vkAllocateCommandBuffers(m_ctx.device(), &cai, cmds.data()));

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signalled so first frame doesn't wait forever

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& f = m_frames[i];
        f.cmd = cmds[i];
        VK_CHECK(vkCreateSemaphore(m_ctx.device(), &si, nullptr, &f.imageAvail));
        VK_CHECK(vkCreateSemaphore(m_ctx.device(), &si, nullptr, &f.renderDone));
        VK_CHECK(vkCreateFence(m_ctx.device(), &fi, nullptr, &f.inFlight));

        // Per-frame UBO (host visible, persistently mapped)
        f.ubo = VulkanBuffer::createHostVisible(m_ctx,
            sizeof(UBOFrame),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        // UI vertex buffer (dynamic, overwritten every frame)
        f.uiVB = VulkanBuffer::createHostVisible(m_ctx,
            UI_MAX_VERTS * sizeof(VertexUI),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
}

void VulkanRenderer::destroyFrameData() {
    for (auto& f : m_frames) {
        if (f.inFlight)   vkDestroyFence(m_ctx.device(), f.inFlight, nullptr);
        if (f.renderDone) vkDestroySemaphore(m_ctx.device(), f.renderDone, nullptr);
        if (f.imageAvail) vkDestroySemaphore(m_ctx.device(), f.imageAvail, nullptr);
        f.ubo.destroy(m_ctx.device());
        f.uiVB.destroy(m_ctx.device());
    }
}

void VulkanRenderer::registerFontAtlas(VkImageView view, VkSampler sampler) {
    m_fontView    = view;
    m_fontSampReg = sampler;
    // (Re)allocate UI descriptor sets for all frames
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& f = m_frames[i];
        f.uiSet = m_pipeline.allocateUISet(m_ctx.device(), view, sampler);
    }
}

void VulkanRenderer::updateFrameUBO(const UBOFrame& ubo) {
    m_frames[m_currentFrame].ubo.update(&ubo, sizeof(UBOFrame));
}

// ── Resize ─────────────────────────────────────────────────────────────────
void VulkanRenderer::resize(uint32_t w, uint32_t h) {
    m_pendingW     = w;
    m_pendingH     = h;
    m_needsResize  = true;
}

void VulkanRenderer::recreateSwapchain() {
    vkDeviceWaitIdle(m_ctx.device());
    m_swapchain.create(m_ctx, m_pendingW, m_pendingH,
        m_vsync, m_pipeline.mainRenderPass());
    m_needsResize = false;
    // Re-register font atlas in case UI sets need updating (they're already valid)
    LOG_INFO(Fmt("Swapchain recreated: %ux%u", m_pendingW, m_pendingH));
}

// ── Frame ──────────────────────────────────────────────────────────────────
bool VulkanRenderer::beginFrame() {
    if (m_needsResize) recreateSwapchain();

    auto& f = m_frames[m_currentFrame];
    vkWaitForFences(m_ctx.device(), 1, &f.inFlight, VK_TRUE, UINT64_MAX);

    VkResult res = m_swapchain.acquireNextImage(m_ctx.device(),
        f.imageAvail, m_imageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        m_needsResize = true;
        return false;
    } else if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    vkResetFences(m_ctx.device(), 1, &f.inFlight);

    // Allocate scene descriptor set if not done yet
    if (f.sceneSet == VK_NULL_HANDLE) {
        f.sceneSet = m_pipeline.allocateSceneSet(m_ctx.device(),
            f.ubo.buffer, sizeof(UBOFrame),
            m_pipeline.shadowDepth(), m_shadowSampler);
    }

    // Begin command buffer recording
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));

    // ── Shadow pass ────────────────────────────────────────────────────
    beginShadowPass();

    m_inFrame = true;
    return true;
}

void VulkanRenderer::beginShadowPass() {
    auto& f = m_frames[m_currentFrame];
    VkClearValue clearDepth{ {1.0f, 0} };

    VkRenderPassBeginInfo rbi{};
    rbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass        = m_pipeline.shadowRenderPass();
    rbi.framebuffer       = m_pipeline.shadowFramebuffer();
    rbi.renderArea.extent = { m_pipeline.shadowDim(), m_pipeline.shadowDim() };
    rbi.clearValueCount   = 1;
    rbi.pClearValues      = &clearDepth;
    vkCmdBeginRenderPass(f.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.shadowPipeline());
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline.sceneLayout(), 0, 1, &f.sceneSet, 0, nullptr);

    VkViewport vp{ 0, 0,
        (float)m_pipeline.shadowDim(), (float)m_pipeline.shadowDim(),
        0.0f, 1.0f };
    VkRect2D sc{ {0,0}, { m_pipeline.shadowDim(), m_pipeline.shadowDim() } };
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    m_shadowPassActive = true;
}

void VulkanRenderer::endShadowPass() {
    auto& f = m_frames[m_currentFrame];
    if (m_shadowPassActive) {
        vkCmdEndRenderPass(f.cmd);
        m_shadowPassActive = false;
    }
}

void VulkanRenderer::beginMainPass() {
    auto& f = m_frames[m_currentFrame];
    endShadowPass(); // shadow must be done before main starts

    std::array<VkClearValue, 2> clears{};
    clears[0].color        = { m_skyR, m_skyG, m_skyB, 1.0f }; // live sky color
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rbi{};
    rbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass        = m_pipeline.mainRenderPass();
    rbi.framebuffer       = m_swapchain.framebuffer(m_imageIndex);
    rbi.renderArea.extent = m_swapchain.extent();
    rbi.clearValueCount   = (uint32_t)clears.size();
    rbi.pClearValues      = clears.data();
    vkCmdBeginRenderPass(f.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.scenePipeline());
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline.sceneLayout(), 0, 1, &f.sceneSet, 0, nullptr);

    VkExtent2D ext = m_swapchain.extent();
    VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
    VkRect2D   sc{ {0,0}, ext };
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    m_mainPassActive = true;
}

void VulkanRenderer::endMainPass() {
    auto& f = m_frames[m_currentFrame];
    if (m_mainPassActive) {
        vkCmdEndRenderPass(f.cmd);
        m_mainPassActive = false;
    }
}

// ── Draw calls ─────────────────────────────────────────────────────────────
void VulkanRenderer::drawMesh(VkBuffer vb, VkBuffer ib, uint32_t idxCount,
                               const glm::mat4& model, bool shadow)
{
    // Untextured path — bind the white 1x1 fallback as the diffuse set.
    // Shader sees: vertex_color * white = vertex_color. Nothing changes visually.
    drawMeshTextured(vb, ib, idxCount, model, m_whiteDiffuseSet, shadow);
}

void VulkanRenderer::drawMeshTextured(VkBuffer vb, VkBuffer ib, uint32_t idxCount,
                                       const glm::mat4& model,
                                       VkDescriptorSet diffuseSet, bool shadow)
{
    if (!m_inFrame) return;
    auto& f = m_frames[m_currentFrame];

    if (shadow && !m_shadowPassActive) {
        // Already started in beginFrame
    } else if (!shadow && !m_mainPassActive) {
        beginMainPass();
    }

    PushConst pc{};
    pc.model     = model;
    pc.normalMat = glm::transpose(glm::inverse(model));

    VkPipelineLayout layout = m_pipeline.sceneLayout();
    vkCmdPushConstants(f.cmd, layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConst), &pc);

    // Bind set 1: diffuse texture (or white fallback)
    if (!shadow && diffuseSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout, 1, 1, &diffuseSet, 0, nullptr);
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(f.cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(f.cmd, ib, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(f.cmd, idxCount, 1, 0, 0, 0);
}

VkDescriptorSet VulkanRenderer::allocateDiffuseSet(VkImageView view, VkSampler sampler) const {
    return m_pipeline.allocateDiffuseSet(m_ctx.device(), view, sampler);
}

// ── UI batch submission ────────────────────────────────────────────────────
void VulkanRenderer::submitUI(UIBatch& batch) {
    if (!m_inFrame) return;
    if (!m_mainPassActive) beginMainPass();

    auto& f    = m_frames[m_currentFrame];
    auto  verts = batch.vertices();
    if (verts.empty()) return;

    uint32_t count = (uint32_t)std::min((size_t)UI_MAX_VERTS, verts.size());
    f.uiVB.update(verts.data(), count * sizeof(VertexUI));

    // Switch to UI pipeline
    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.uiPipeline());
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline.uiLayout(), 0, 1, &f.uiSet, 0, nullptr);

    // Draw solid quads first (mode=0), then text quads (mode=1)
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.uiVB.buffer, &offset);

    uint32_t solidCount = batch.solidCount();
    uint32_t textCount  = (uint32_t)verts.size() - solidCount;

    if (solidCount > 0) {
        UIPushConst pc{ 0 };
        vkCmdPushConstants(f.cmd, m_pipeline.uiLayout(),
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPushConst), &pc);
        vkCmdDraw(f.cmd, solidCount, 1, 0, 0);
    }
    if (textCount > 0) {
        UIPushConst pc{ 1 };
        vkCmdPushConstants(f.cmd, m_pipeline.uiLayout(),
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPushConst), &pc);
        vkCmdDraw(f.cmd, textCount, 1, solidCount, 0);
    }

    batch.clear();
}

// ── End frame ─────────────────────────────────────────────────────────────
void VulkanRenderer::endFrame() {
    if (!m_inFrame) return;
    auto& f = m_frames[m_currentFrame];

    endMainPass();
    VK_CHECK(vkEndCommandBuffer(f.cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &f.imageAvail;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &f.renderDone;
    VK_CHECK(vkQueueSubmit(m_ctx.graphicsQ(), 1, &si, f.inFlight));

    VkResult res = m_swapchain.present(m_ctx.presentQ(),
        m_imageIndex, f.renderDone);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        m_needsResize = true;

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_inFrame = false;
}

// ── Shutdown ──────────────────────────────────────────────────────────────
void VulkanRenderer::shutdown() {
    if (m_ctx.device()) vkDeviceWaitIdle(m_ctx.device());

    destroyFrameData();
    m_whiteImage.destroy(m_ctx.device());
    if (m_shadowSampler) vkDestroySampler(m_ctx.device(), m_shadowSampler, nullptr);
    if (m_fontSampler)   vkDestroySampler(m_ctx.device(), m_fontSampler,   nullptr);

    m_pipeline.destroy(m_ctx.device());
    m_swapchain.destroy(m_ctx.device());
    m_ctx.destroy();
}
