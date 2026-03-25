// VulkanSwapchain.cpp
#include "VulkanSwapchain.h"
#include <algorithm>
#include <limits>

VulkanSwapchain::~VulkanSwapchain() {}

void VulkanSwapchain::create(const VulkanContext& ctx,
                             uint32_t desiredW, uint32_t desiredH,
                             bool vsync,
                             VkRenderPass renderPass)
{
    SwapchainSupport sc = ctx.querySwapchainSupport(ctx.physDevice());
    VkSurfaceFormatKHR fmt  = chooseSurfaceFormat(sc.formats);
    VkPresentModeKHR   mode = choosePresentMode(sc.presentModes, vsync);
    VkExtent2D         ext  = chooseExtent(sc.caps, desiredW, desiredH);

    uint32_t imageCount = sc.caps.minImageCount + 1;
    if (sc.caps.maxImageCount > 0 && imageCount > sc.caps.maxImageCount)
        imageCount = sc.caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imageCount;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilies qf = ctx.queueFamilies();
    uint32_t qfIndices[] = { qf.graphics.value(), qf.present.value() };

    if (qf.graphics.value() != qf.present.value()) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = sc.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = mode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = m_swapchain; // lets driver reuse resources on resize

    VkSwapchainKHR newSwap;
    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &newSwap));

    // Destroy old swapchain objects before proceeding
    destroy(ctx.device());

    m_swapchain = newSwap;
    m_format    = fmt.format;
    m_extent    = ext;

    // Get images
    uint32_t cnt;
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &cnt, nullptr);
    m_images.resize(cnt);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &cnt, m_images.data());

    // Create image views
    m_imageViews.resize(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
        m_imageViews[i] = createImageView(ctx.device(), m_images[i],
            m_format, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Create depth image
    m_depth = VulkanImage::createDepth(ctx, ext.width, ext.height);

    // Create framebuffers
    m_framebuffers.resize(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
        std::array<VkImageView, 2> attachments = {
            m_imageViews[i], m_depth.view
        };
        VkFramebufferCreateInfo fci{};
        fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = renderPass;
        fci.attachmentCount = (uint32_t)attachments.size();
        fci.pAttachments    = attachments.data();
        fci.width           = ext.width;
        fci.height          = ext.height;
        fci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(ctx.device(), &fci, nullptr, &m_framebuffers[i]));
    }

    LOG_INFO(Fmt("Swapchain created: %ux%u, %u images, vsync=%s",
        ext.width, ext.height, cnt, vsync ? "on" : "off"));
}

void VulkanSwapchain::destroy(VkDevice device) {
    for (auto& fb : m_framebuffers) if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();

    m_depth.destroy(device);

    for (auto& iv : m_imageViews) if (iv) vkDestroyImageView(device, iv, nullptr);
    m_imageViews.clear();

    m_images.clear();

    if (m_swapchain) vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

VkResult VulkanSwapchain::acquireNextImage(VkDevice device,
                                            VkSemaphore sig,
                                            uint32_t& outIdx)
{
    return vkAcquireNextImageKHR(device, m_swapchain,
        UINT64_MAX, sig, VK_NULL_HANDLE, &outIdx);
}

VkResult VulkanSwapchain::present(VkQueue presentQ,
                                   uint32_t idx,
                                   VkSemaphore wait)
{
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &wait;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &idx;
    return vkQueuePresentKHR(presentQ, &pi);
}

// ── Format / mode / extent selection ─────────────────────────────────────
VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const
{
    // Prefer BGRA8_SRGB with sRGB color space
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR VulkanSwapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes, bool vsync) const
{
    if (!vsync) {
        // Mailbox = low-latency, no tearing; FIFO = vsync
        for (auto& m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
        for (auto& m : modes)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be available
}

VkExtent2D VulkanSwapchain::chooseExtent(
    const VkSurfaceCapabilitiesKHR& caps,
    uint32_t w, uint32_t h) const
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    return {
        std::clamp(w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}
