// VulkanBuffer.cpp
#include "VulkanBuffer.h"

VulkanBuffer makeBuffer(
    const VulkanContext& ctx,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props)
{
    VulkanBuffer result;
    result.size = size;

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx.device(), &bi, nullptr, &result.buffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device(), result.buffer, &mr);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ai, nullptr, &result.memory));
    VK_CHECK(vkBindBufferMemory(ctx.device(), result.buffer, result.memory, 0));

    return result;
}

void copyBuffer(const VulkanContext& ctx,
                VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBuffer cmd = ctx.beginSingleCmd();
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    ctx.endSingleCmd(cmd);
}

VulkanBuffer VulkanBuffer::createDeviceLocal(
    const VulkanContext& ctx,
    const void*          data,
    VkDeviceSize         size,
    VkBufferUsageFlags   usage)
{
    // Staging buffer (CPU visible)
    VulkanBuffer staging = makeBuffer(ctx, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Map, copy, unmap
    void* mapped;
    vkMapMemory(ctx.device(), staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(ctx.device(), staging.memory);

    // Device-local buffer
    VulkanBuffer result = makeBuffer(ctx, size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(ctx, staging.buffer, result.buffer, size);
    staging.destroy(ctx.device());

    return result;
}

VulkanBuffer VulkanBuffer::createHostVisible(
    const VulkanContext& ctx,
    VkDeviceSize         size,
    VkBufferUsageFlags   usage)
{
    VulkanBuffer result = makeBuffer(ctx, size, usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Persistently mapped — great for per-frame UBO updates
    vkMapMemory(ctx.device(), result.memory, 0, size, 0, &result.mapped);
    return result;
}
