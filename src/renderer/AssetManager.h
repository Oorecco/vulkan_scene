#pragma once
// AssetManager.h — Load, cache, and manage GPU textures.
// Uses stb_image to decode PNG/JPEG/BMP into RGBA pixels,
// then hands off to VulkanImage for GPU upload. Caches by filename.
// NOTE: stb_image.h is included ONLY in AssetManager.cpp — not here.
//       Putting it in a header is how you get 50 "multiple definition" errors
//       and a very bad afternoon.

#include "../Common.h"
#include "../renderer/VulkanImage.h"
#include "../renderer/VulkanContext.h"
#include <unordered_map>
#include <string>
#include <atomic>

struct TextureEntry {
    VulkanImage image;
    VkSampler   sampler = VK_NULL_HANDLE; // repeat sampler for world textures
    bool        valid   = false;
};

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager() = default;

    void init(const VulkanContext& ctx, const std::string& assetRoot);
    void destroy();

    // Load a texture from assets/textures/<subpath>.
    // Returns a reference to the cached entry. Creates a white 1x1 on failure.
    const TextureEntry& getTexture(const std::string& subpath);

    // Convenience: get just the image view for descriptor writes
    VkImageView view(const std::string& subpath)    { return getTexture(subpath).image.view; }
    VkSampler   sampler(const std::string& subpath) { return getTexture(subpath).sampler; }

    // Load all textures needed at startup (called from loading screen thread)
    // progressOut increments 0.0→1.0 as each texture loads
    void preloadAll(std::atomic<float>& progressOut);

    // Repeat sampler (used for tiling world textures)
    VkSampler repeatSampler() const { return m_repeatSampler; }
    // Clamp sampler (used for UI / skybox)
    VkSampler clampSampler()  const { return m_clampSampler;  }

private:
    TextureEntry loadFromDisk(const std::string& fullPath, bool srgb = true);
    void createSamplers();

    const VulkanContext*                          m_ctx        = nullptr;
    std::string                                   m_assetRoot;
    std::unordered_map<std::string, TextureEntry> m_cache;
    VkSampler                                     m_repeatSampler = VK_NULL_HANDLE;
    VkSampler                                     m_clampSampler  = VK_NULL_HANDLE;
    TextureEntry                                  m_whiteTexture;

    // Full list of textures to preload on startup
    static const std::vector<std::string> PRELOAD_LIST;
};
