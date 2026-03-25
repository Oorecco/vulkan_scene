// VulkanContext.cpp — "Hello Vulkan" and everything required to say it properly.
#include "VulkanContext.h"
#include <set>
#include <stdexcept>
#include <cstring>

const std::vector<const char*> VulkanContext::DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

const std::vector<const char*> VulkanContext::VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation",
};

#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

// ── Debug messenger callback ───────────────────────────────────────────────
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN(std::string("Vulkan validation: ") + data->pMessage);
    }
    return VK_FALSE;
}

// ── Public init/destroy ───────────────────────────────────────────────────
void VulkanContext::init(HWND hwnd, int versionPref) {
    createInstance(versionPref);
    if (ENABLE_VALIDATION) setupDebugMessenger();
    createSurface(hwnd);
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
    LOG_INFO(Fmt("Vulkan context ready. API version %d.%d.%d",
        VK_VERSION_MAJOR(m_apiVersion),
        VK_VERSION_MINOR(m_apiVersion),
        VK_VERSION_PATCH(m_apiVersion)));
}

void VulkanContext::destroy() {
    if (m_cmdPool)   vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    if (m_device)    vkDestroyDevice(m_device, nullptr);
    if (m_surface)   vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (ENABLE_VALIDATION && m_debugMessenger) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, m_debugMessenger, nullptr);
    }
    if (m_instance)  vkDestroyInstance(m_instance, nullptr);
}

VulkanContext::~VulkanContext() { destroy(); }

// ── Instance creation with version negotiation ────────────────────────────
void VulkanContext::createInstance(int versionPref) {
    // Try preferred version first, fall back down to 1.1
    // versionPref: 0 = prefer 1.3, 1 = prefer 1.2, 2 = prefer 1.1
    static const uint32_t versions[] = {
        VK_API_VERSION_1_3, VK_API_VERSION_1_2, VK_API_VERSION_1_1
    };
    uint32_t startIdx = std::min((int)versionPref, 2);

    for (int i = (int)startIdx; i < 3; ++i) {
        // Check if the loader supports this version
        uint32_t loaderVersion = 0;
        auto fnEnum = (PFN_vkEnumerateInstanceVersion)
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
        if (fnEnum) fnEnum(&loaderVersion);
        else loaderVersion = VK_API_VERSION_1_0;

        if (loaderVersion < versions[i]) continue;

        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Vulkan 1.1+ Scene";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 7, 0);
        appInfo.pEngineName        = "VkScene Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = versions[i];

        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };
        if (ENABLE_VALIDATION) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &appInfo;
        ci.enabledExtensionCount   = (uint32_t)extensions.size();
        ci.ppEnabledExtensionNames = extensions.data();

        if (ENABLE_VALIDATION) {
            ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
            ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }

        VkResult res = vkCreateInstance(&ci, nullptr, &m_instance);
        if (res == VK_SUCCESS) {
            m_apiVersion = versions[i];
            LOG_INFO(Fmt("Vulkan instance created with API version %d.%d",
                VK_VERSION_MAJOR(versions[i]), VK_VERSION_MINOR(versions[i])));
            return;
        }
    }

    throw std::runtime_error(
        "Fatal: Vulkan is not available on this system.\n"
        "Requires at least Vulkan 1.1. Please update your GPU drivers.");
}

void VulkanContext::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;

    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn) fn(m_instance, &ci, nullptr, &m_debugMessenger);
}

void VulkanContext::createSurface(HWND hwnd) {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hwnd      = hwnd;
    ci.hinstance = GetModuleHandle(nullptr);
    VK_CHECK(vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &m_surface));
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("Fatal: No Vulkan-capable GPU found.");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU, then integrated
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;

    for (auto& dev : devices) {
        if (!isDeviceSuitable(dev)) continue;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 2;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 1;

        if (score > bestScore) { bestScore = score; best = dev; }
    }

    if (best == VK_NULL_HANDLE)
        throw std::runtime_error("Fatal: No suitable Vulkan GPU found.");

    m_physDevice = best;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    LOG_INFO(Fmt("Selected GPU: %s", props.deviceName));
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice dev) const {
    QueueFamilies qf = findQueueFamilies(dev);
    if (!qf.complete()) return false;
    if (!checkDeviceExtensions(dev)) return false;
    SwapchainSupport sc = querySwapchainSupport(dev);
    return !sc.formats.empty() && !sc.presentModes.empty();
}

bool VulkanContext::checkDeviceExtensions(VkPhysicalDevice dev) const {
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, available.data());

    std::set<std::string> required(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
    for (auto& ext : available) required.erase(ext.extensionName);
    return required.empty();
}

void VulkanContext::createLogicalDevice() {
    m_queueFamilies = findQueueFamilies(m_physDevice);

    std::set<uint32_t> uniqueQF = {
        m_queueFamilies.graphics.value(),
        m_queueFamilies.present.value()
    };

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCIs;
    for (uint32_t qf : uniqueQF) {
        VkDeviceQueueCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = qf;
        ci.queueCount       = 1;
        ci.pQueuePriorities = &priority;
        queueCIs.push_back(ci);
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy        = VK_TRUE;
    features.depthClamp               = VK_TRUE;
    features.fillModeNonSolid         = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = (uint32_t)queueCIs.size();
    ci.pQueueCreateInfos       = queueCIs.data();
    ci.enabledExtensionCount   = (uint32_t)DEVICE_EXTENSIONS.size();
    ci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
    ci.pEnabledFeatures        = &features;

    if (ENABLE_VALIDATION) {
        ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
        ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }

    VK_CHECK(vkCreateDevice(m_physDevice, &ci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_queueFamilies.graphics.value(), 0, &m_graphicsQ);
    vkGetDeviceQueue(m_device, m_queueFamilies.present.value(),  0, &m_presentQ);
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_queueFamilies.graphics.value();
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool));
}

// ── Queue family search ───────────────────────────────────────────────────
QueueFamilies VulkanContext::findQueueFamilies(VkPhysicalDevice dev) const {
    QueueFamilies result;
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            result.graphics = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &presentSupport);
        if (presentSupport) result.present = i;

        if (result.complete()) break;
    }
    return result;
}

// ── Swapchain support query ───────────────────────────────────────────────
SwapchainSupport VulkanContext::querySwapchainSupport(VkPhysicalDevice dev) const {
    SwapchainSupport result;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, m_surface, &result.caps);

    uint32_t cnt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surface, &cnt, nullptr);
    result.formats.resize(cnt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surface, &cnt, result.formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surface, &cnt, nullptr);
    result.presentModes.resize(cnt);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surface, &cnt, result.presentModes.data());

    return result;
}

// ── One-shot command helpers ───────────────────────────────────────────────
VkCommandBuffer VulkanContext::beginSingleCmd() const {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void VulkanContext::endSingleCmd(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;

    vkQueueSubmit(m_graphicsQ, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQ);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);
}

// ── Memory type finder ────────────────────────────────────────────────────
uint32_t VulkanContext::findMemoryType(uint32_t typeBits,
                                       VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type.");
}

// ── Depth format ──────────────────────────────────────────────────────────
VkFormat VulkanContext::findDepthFormat() const {
    // Prefer D32, fall back to D24 or D16
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physDevice, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    throw std::runtime_error("Failed to find supported depth format.");
}

bool VulkanContext::hasStencilComponent(VkFormat fmt) const {
    return fmt == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           fmt == VK_FORMAT_D24_UNORM_S8_UINT;
}
