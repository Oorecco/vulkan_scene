// AssetManager.cpp — Where disk files become GPU pixels.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "AssetManager.h"
#include <stdexcept>

// All textures we want loaded before the game starts
const std::vector<std::string> AssetManager::PRELOAD_LIST = {
    "textures/ground/ground_grass.png",
    "textures/ground/ground_dirt.png",
    "textures/ground/ground_alt.png",
    "textures/skybox/skybox_front.png",
    "textures/skybox/skybox_back.png",
    "textures/skybox/skybox_left.png",
    "textures/skybox/skybox_right.png",
    "textures/skybox/skybox_top.png",
    "textures/skybox/skybox_bottom.png",
    "textures/props/bark.png",
    "textures/props/leaves.png",
    "textures/props/rock.png",
    "textures/props/gray_rocks.png",
    "textures/props/stone.png",
    "textures/props/mushroom_cap.png",
    "textures/props/mushroom_stem.png",
    "textures/props/crate.png",
    "textures/props/fence_wood.png",
    "textures/props/sign_wood.png",
    "textures/props/cube_diffuse.png",
    "textures/props/cursed_geometry.png",
    "textures/props/grass_2d.png",
    "textures/props/wood_tree.png",
};

void AssetManager::init(const VulkanContext& ctx, const std::string& assetRoot) {
    m_ctx       = &ctx;
    m_assetRoot = assetRoot;
    if (m_assetRoot.back() != '/' && m_assetRoot.back() != '\\')
        m_assetRoot += '/';

    createSamplers();

    // White fallback texture (1x1 opaque white)
    m_whiteTexture.image = VulkanImage::createWhite(ctx);
    m_whiteTexture.sampler = m_clampSampler;
    m_whiteTexture.valid   = true;
}

void AssetManager::createSamplers() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_ctx->physDevice(), &props);
    float maxAniso = std::min(props.limits.maxSamplerAnisotropy, 4.0f); // cap at 4x

    // Repeat (tiling) — used for all world textures
    {
        VkSamplerCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter        = VK_FILTER_LINEAR;
        ci.minFilter        = VK_FILTER_LINEAR;
        ci.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.anisotropyEnable = VK_TRUE;
        ci.maxAnisotropy    = maxAniso;
        ci.maxLod           = VK_LOD_CLAMP_NONE;
        VK_CHECK(vkCreateSampler(m_ctx->device(), &ci, nullptr, &m_repeatSampler));
    }
    // Clamp — used for skybox faces, UI, icons
    {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = VK_FILTER_LINEAR;
        ci.minFilter    = VK_FILTER_LINEAR;
        ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        VK_CHECK(vkCreateSampler(m_ctx->device(), &ci, nullptr, &m_clampSampler));
    }
}

TextureEntry AssetManager::loadFromDisk(const std::string& fullPath, bool srgb) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(0); // Vulkan UV origin is top-left

    uint8_t* pixels = stbi_load(fullPath.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_WARN("AssetManager: failed to load '" + fullPath + "' - " + stbi_failure_reason());
        return m_whiteTexture; // return white so rendering continues
    }

    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VulkanImage img = VulkanImage::createFromPixels(*m_ctx,
        (uint32_t)w, (uint32_t)h, pixels, fmt);
    stbi_image_free(pixels);

    TextureEntry entry;
    entry.image   = img;
    entry.sampler = m_repeatSampler; // default; callers can override
    entry.valid   = (img.image != VK_NULL_HANDLE);
    return entry;
}

const TextureEntry& AssetManager::getTexture(const std::string& subpath) {
    auto it = m_cache.find(subpath);
    if (it != m_cache.end()) return it->second;

    // Not cached — load it
    std::string fullPath = m_assetRoot + subpath;
    TextureEntry entry = loadFromDisk(fullPath);
    m_cache[subpath] = std::move(entry);
    return m_cache[subpath];
}

void AssetManager::preloadAll(std::atomic<float>& progress) {
    float total = (float)PRELOAD_LIST.size();
    for (int i = 0; i < (int)PRELOAD_LIST.size(); i++) {
        getTexture(PRELOAD_LIST[i]);
        progress.store((float)(i + 1) / total);
    }
}

void AssetManager::destroy() {
    for (auto& [key, entry] : m_cache)
        entry.image.destroy(m_ctx->device());
    m_cache.clear();
    m_whiteTexture.image.destroy(m_ctx->device());

    if (m_repeatSampler) vkDestroySampler(m_ctx->device(), m_repeatSampler, nullptr);
    if (m_clampSampler)  vkDestroySampler(m_ctx->device(), m_clampSampler,  nullptr);
    m_repeatSampler = m_clampSampler = VK_NULL_HANDLE;
}
