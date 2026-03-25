#pragma once
// VulkanSwapchain.h — Swapchain, image views, framebuffers, and present logic.
// Needs to be rebuilt on window resize. Holds references to the main depth image.

#include "../Common.h"
#include "VulkanContext.h"
#include "VulkanImage.h"

class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    // Create or recreate the swapchain (called on init and resize)
    void create(const VulkanContext& ctx,
                uint32_t            desiredWidth,
                uint32_t            desiredHeight,
                bool                vsync,
                VkRenderPass        renderPass);

    void destroy(VkDevice device);

    // Returns VK_ERROR_OUT_OF_DATE_KHR if resize is needed
    VkResult acquireNextImage(VkDevice device,
                              VkSemaphore signalSemaphore,
                              uint32_t&   outIndex);

    VkResult present(VkQueue presentQ,
                     uint32_t imageIndex,
                     VkSemaphore waitSemaphore);

    // ── Getters ───────────────────────────────────────────────────────────
    VkSwapchainKHR     swapchain()              const { return m_swapchain; }
    VkFormat           format()                 const { return m_format; }
    VkExtent2D         extent()                 const { return m_extent; }
    uint32_t           imageCount()             const { return (uint32_t)m_imageViews.size(); }
    VkFramebuffer      framebuffer(uint32_t i)  const { return m_framebuffers[i]; }

private:
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>&) const;
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>&, bool vsync) const;
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR&,
                                    uint32_t w, uint32_t h) const;

    VkSwapchainKHR              m_swapchain   = VK_NULL_HANDLE;
    VkFormat                    m_format      = VK_FORMAT_UNDEFINED;
    VkExtent2D                  m_extent      = {};
    std::vector<VkImage>        m_images;
    std::vector<VkImageView>    m_imageViews;
    std::vector<VkFramebuffer>  m_framebuffers;
    VulkanImage                 m_depth;
};
