#pragma once
// VulkanBuffer.h — Buffer creation, staging uploads, and lifetime management.
// Vulkan buffers don't upload themselves — that's what the staging buffer is for.

#include "../Common.h"
#include "VulkanContext.h"

struct VulkanBuffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr; // non-null if persistently mapped

    // Upload data to a device-local buffer via staging (for vertex/index data)
    static VulkanBuffer createDeviceLocal(
        const VulkanContext& ctx,
        const void*          data,
        VkDeviceSize         size,
        VkBufferUsageFlags   usage);

    // Create a host-visible buffer (for UBOs, updated every frame)
    static VulkanBuffer createHostVisible(
        const VulkanContext& ctx,
        VkDeviceSize         size,
        VkBufferUsageFlags   usage);

    void update(const void* data, VkDeviceSize sz) const {
        memcpy(mapped, data, sz);
    }

    void destroy(VkDevice device) {
        if (mapped)   vkUnmapMemory(device, memory);
        if (buffer)   vkDestroyBuffer(device, buffer, nullptr);
        if (memory)   vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        mapped = nullptr;
    }
};

// Helper to create and fill a raw buffer
VulkanBuffer makeBuffer(
    const VulkanContext& ctx,
    VkDeviceSize         size,
    VkBufferUsageFlags   usage,
    VkMemoryPropertyFlags props);

void copyBuffer(
    const VulkanContext& ctx,
    VkBuffer             src,
    VkBuffer             dst,
    VkDeviceSize         size);
