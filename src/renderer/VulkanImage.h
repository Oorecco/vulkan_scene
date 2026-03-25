#pragma once
// VulkanImage.h — Image objects: depth buffers, shadow maps, textures.
// Vulkan images need explicit layout transitions. We handle that here.

#include "../Common.h"
#include "VulkanContext.h"

struct VulkanImage {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkFormat       format  = VK_FORMAT_UNDEFINED;
    uint32_t       width   = 0;
    uint32_t       height  = 0;

    // Create a depth attachment (for main pass or shadow map)
    static VulkanImage createDepth(
        const VulkanContext& ctx,
        uint32_t w, uint32_t h,
        VkFormat fmt = VK_FORMAT_UNDEFINED); // VK_FORMAT_UNDEFINED = auto-pick

    // Create a sampled image from raw pixel data (R8G8B8A8_SRGB)
    static VulkanImage createFromPixels(
        const VulkanContext& ctx,
        uint32_t w, uint32_t h,
        const uint8_t* pixels, // RGBA, w*h*4 bytes
        VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB);

    // Create a 1x1 white placeholder texture
    static VulkanImage createWhite(const VulkanContext& ctx);

    // Create an R8_UNORM image for the font atlas
    static VulkanImage createFontAtlas(
        const VulkanContext& ctx,
        uint32_t w, uint32_t h,
        const uint8_t* pixels); // R8 data

    void transitionLayout(const VulkanContext& ctx,
                          VkImageLayout oldLayout,
                          VkImageLayout newLayout);

    void destroy(VkDevice device) {
        if (view)   vkDestroyImageView(device, view, nullptr);
        if (image)  vkDestroyImage(device, image, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
};

// Create a VkImageView for the given image
VkImageView createImageView(VkDevice device,
                             VkImage image,
                             VkFormat format,
                             VkImageAspectFlags aspectFlags);

// Create a raw VkImage + allocate its memory
VulkanImage allocImage(const VulkanContext& ctx,
                       uint32_t w, uint32_t h,
                       VkFormat fmt,
                       VkImageUsageFlags usage,
                       VkMemoryPropertyFlags memProps);
