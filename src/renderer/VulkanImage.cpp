// VulkanImage.cpp
#include "VulkanImage.h"
#include "VulkanBuffer.h"

// ── Internal helpers ──────────────────────────────────────────────────────
VkImageView createImageView(VkDevice device, VkImage image,
                             VkFormat format, VkImageAspectFlags aspects)
{
    VkImageViewCreateInfo ci{};
    ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image                           = image;
    ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ci.format                          = format;
    ci.subresourceRange.aspectMask     = aspects;
    ci.subresourceRange.baseMipLevel   = 0;
    ci.subresourceRange.levelCount     = 1;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount     = 1;

    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &ci, nullptr, &view));
    return view;
}

VulkanImage allocImage(const VulkanContext& ctx,
                       uint32_t w, uint32_t h,
                       VkFormat fmt, VkImageUsageFlags usage,
                       VkMemoryPropertyFlags memProps)
{
    VulkanImage img;
    img.width  = w;
    img.height = h;
    img.format = fmt;

    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = fmt;
    ci.extent        = { w, h, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(ctx.device(), &ci, nullptr, &img.image));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device(), img.image, &mr);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits, memProps);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ai, nullptr, &img.memory));
    VK_CHECK(vkBindImageMemory(ctx.device(), img.image, img.memory, 0));

    return img;
}

// ── Layout transition ─────────────────────────────────────────────────────
void VulkanImage::transitionLayout(const VulkanContext& ctx,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout)
{
    VkCommandBuffer cmd = ctx.beginSingleCmd();

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange.aspectMask =
        (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
         oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    ctx.endSingleCmd(cmd);
}

// ── Factory methods ───────────────────────────────────────────────────────
VulkanImage VulkanImage::createDepth(const VulkanContext& ctx,
                                     uint32_t w, uint32_t h,
                                     VkFormat fmt)
{
    if (fmt == VK_FORMAT_UNDEFINED) fmt = ctx.findDepthFormat();

    VulkanImage img = allocImage(ctx, w, h, fmt,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    img.view = createImageView(ctx.device(), img.image,
        fmt, VK_IMAGE_ASPECT_DEPTH_BIT);

    img.transitionLayout(ctx,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    return img;
}

VulkanImage VulkanImage::createFromPixels(const VulkanContext& ctx,
                                          uint32_t w, uint32_t h,
                                          const uint8_t* pixels,
                                          VkFormat fmt)
{
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    VulkanBuffer staging = makeBuffer(ctx, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkMapMemory(ctx.device(), staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, pixels, size);
    vkUnmapMemory(ctx.device(), staging.memory);

    VulkanImage img = allocImage(ctx, w, h, fmt,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    img.transitionLayout(ctx,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer → image
    {
        VkCommandBuffer cmd = ctx.beginSingleCmd();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { w, h, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        ctx.endSingleCmd(cmd);
    }

    img.transitionLayout(ctx,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    img.view = createImageView(ctx.device(), img.image,
        fmt, VK_IMAGE_ASPECT_COLOR_BIT);

    staging.destroy(ctx.device());
    return img;
}

VulkanImage VulkanImage::createWhite(const VulkanContext& ctx) {
    uint8_t white[4] = { 255, 255, 255, 255 };
    return createFromPixels(ctx, 1, 1, white);
}

VulkanImage VulkanImage::createFontAtlas(const VulkanContext& ctx,
                                         uint32_t w, uint32_t h,
                                         const uint8_t* pixels)
{
    VkDeviceSize size = (VkDeviceSize)w * h;

    VulkanBuffer staging = makeBuffer(ctx, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped;
    vkMapMemory(ctx.device(), staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, pixels, size);
    vkUnmapMemory(ctx.device(), staging.memory);

    VulkanImage img = allocImage(ctx, w, h, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    img.transitionLayout(ctx,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    {
        VkCommandBuffer cmd = ctx.beginSingleCmd();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { w, h, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        ctx.endSingleCmd(cmd);
    }
    img.transitionLayout(ctx,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    img.view = createImageView(ctx.device(), img.image,
        VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    staging.destroy(ctx.device());
    return img;
}
