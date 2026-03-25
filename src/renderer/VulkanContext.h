#pragma once
// VulkanContext.h — Instance, physical/logical device, queues, command pool.
// The foundation everything else builds on. If this is broken, nothing works.

#include "../Common.h"
#include <string>
#include <vector>
#include <optional>

// ── Queue families ────────────────────────────────────────────────────────
struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool complete() const {
        return graphics.has_value() && present.has_value();
    }
};

// ── Swapchain support details ─────────────────────────────────────────────
struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR        caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    // Initialise: creates instance → surface → device → command pool
    // vulkanVersionPref: 0=prefer1.3 1=prefer1.2 2=prefer1.1
    void init(HWND hwnd, int vulkanVersionPref);
    void destroy();

    // ── Accessors ─────────────────────────────────────────────────────────
    VkInstance       instance()   const { return m_instance; }
    VkDevice         device()     const { return m_device; }
    VkPhysicalDevice physDevice() const { return m_physDevice; }
    VkSurfaceKHR     surface()    const { return m_surface; }
    VkQueue          graphicsQ()  const { return m_graphicsQ; }
    VkQueue          presentQ()   const { return m_presentQ; }
    VkCommandPool    cmdPool()    const { return m_cmdPool; }
    QueueFamilies    queueFamilies() const { return m_queueFamilies; }
    uint32_t         apiVersion() const { return m_apiVersion; }

    SwapchainSupport querySwapchainSupport(VkPhysicalDevice dev) const;
    QueueFamilies    findQueueFamilies(VkPhysicalDevice dev) const;

    // ── One-shot command helpers ───────────────────────────────────────────
    VkCommandBuffer beginSingleCmd() const;
    void            endSingleCmd(VkCommandBuffer cmd) const;

    // ── Memory ────────────────────────────────────────────────────────────
    uint32_t findMemoryType(uint32_t typeBits,
                            VkMemoryPropertyFlags props) const;

    // ── Format helpers ────────────────────────────────────────────────────
    VkFormat findDepthFormat() const;
    bool     hasStencilComponent(VkFormat fmt) const;

private:
    void createInstance(int versionPref);
    void setupDebugMessenger();
    void createSurface(HWND hwnd);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    bool isDeviceSuitable(VkPhysicalDevice dev) const;
    bool checkDeviceExtensions(VkPhysicalDevice dev) const;

    VkInstance       m_instance    = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface     = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice  = VK_NULL_HANDLE;
    VkDevice         m_device      = VK_NULL_HANDLE;
    VkQueue          m_graphicsQ   = VK_NULL_HANDLE;
    VkQueue          m_presentQ    = VK_NULL_HANDLE;
    VkCommandPool    m_cmdPool     = VK_NULL_HANDLE;
    QueueFamilies    m_queueFamilies;
    uint32_t         m_apiVersion  = VK_API_VERSION_1_1;

    // Debug messenger (debug builds only)
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    static const std::vector<const char*> DEVICE_EXTENSIONS;
    static const std::vector<const char*> VALIDATION_LAYERS;
};
