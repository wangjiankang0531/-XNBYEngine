#define VMA_IMPLEMENTATION
#include "VulkanDevice.h"
#include "hongtu/rhi/TextureUploadLayout.h"
#include "VulkanCommandBuffer.h"
#include "VulkanOffscreenTarget.h"
#include <vulkan/vulkan_core.h>
#include <iostream>
#include <cstring>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace HongTu::RHI::Vulkan {

// ============================================================================
// Static Debug Callback
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    std::cerr << "[Vulkan Debug] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

// ============================================================================
// VulkanBuffer Implementation
// ============================================================================

void* VulkanBuffer::map() {
    if (mappedData) return mappedData;
    vmaMapMemory(allocator, allocation, &mappedData);
    return mappedData;
}

void VulkanBuffer::unmap() {
    if (mappedData) {
        vmaUnmapMemory(allocator, allocation);
        mappedData = nullptr;
    }
}

// ============================================================================
// VulkanTexture Implementation
// ============================================================================

VulkanTexture::VulkanTexture(
    VkImage image,
    VkImageView imageView,
    VmaAllocation allocation,
    const Extent3D& extent,
    Format format,
    TextureUsageFlags usage,
    uint32_t mipLevels,
    uint32_t arrayLayers,
    VkDevice device,
    VmaAllocator allocator
)
    : image(image), imageView(imageView), allocation(allocation),
      extent(extent), format(format), usage(usage),
      mipLevels(mipLevels), arrayLayers(arrayLayers), device(device), allocator(allocator) {}

VulkanTexture::~VulkanTexture() {
    if (imageView != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    if (image != VK_NULL_HANDLE && allocation != nullptr && allocator != nullptr) {
        vmaDestroyImage(allocator, image, allocation);
    }
}

RHITextureViewPtr VulkanTexture::createView() {
    return std::make_shared<VulkanTextureView>(imageView);
}

// ============================================================================
// VulkanFence Implementation
// ============================================================================

bool VulkanFence::isSignaled() const {
    return vkGetFenceStatus(device, fence) == VK_SUCCESS;
}

void VulkanFence::wait() {
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
}

void VulkanFence::reset() {
    vkResetFences(device, 1, &fence);
}

// ============================================================================
// VulkanEvent Implementation
// ============================================================================

bool VulkanEvent::isSignaled() const {
    return vkGetEventStatus(device, event) == VK_EVENT_SET;
}

void VulkanEvent::set() {
    vkSetEvent(device, event);
}

void VulkanEvent::reset() {
    vkResetEvent(device, event);
}

// ============================================================================
// VulkanDevice Implementation
// ============================================================================

VulkanDevice::~VulkanDevice() {
  if (device) {
    vkDeviceWaitIdle(device);
    cleanupSwapchain();

    {
      std::lock_guard lock(descriptorLifetime->mutex);
      descriptorLifetime->device = VK_NULL_HANDLE;
      if (descriptorPool)
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
      for (VkDescriptorPool pool : overflowPools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
      }
    }
    if (commandPool)
      vkDestroyCommandPool(device, commandPool, nullptr);
    if (captureBuffer)
      vmaDestroyBuffer(allocator, captureBuffer, captureAllocation);
    if (allocator)
      vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
  }

  if (surface)
    vkDestroySurfaceKHR(instance, surface, nullptr);
  if (debugMessenger) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func)
      func(instance, debugMessenger, nullptr);
  }
  if (instance)
    vkDestroyInstance(instance, nullptr);
}

bool VulkanDevice::initializeVulkan(
    bool enableValidation,
    const std::vector<const char*>& requiredExtensions,
    void* windowHandle,
    const SwapchainDesc* swapchainDesc
) {
    if (!createInstance(enableValidation, requiredExtensions)) {
        std::cerr << "Failed to create Vulkan instance" << std::endl;
        return false;
    }

    if (enableValidation) setupDebugMessenger();

    if (windowHandle && swapchainDesc) {
        if (!createSurface(windowHandle)) {
            std::cerr << "Failed to create surface" << std::endl;
            return false;
        }
    }

    if (!selectPhysicalDevice()) {
        std::cerr << "Failed to select physical device" << std::endl;
        return false;
    }

    if (!createLogicalDevice()) {
        std::cerr << "Failed to create logical device" << std::endl;
        return false;
    }

    if (!createAllocator()) {
        std::cerr << "Failed to create memory allocator" << std::endl;
        return false;
    }

    if (!createCommandPool()) {
        std::cerr << "Failed to create command pool" << std::endl;
        return false;
    }

    if (!createDescriptorPool()) {
        std::cerr << "Failed to create descriptor pool" << std::endl;
        return false;
    }

    if (surface != VK_NULL_HANDLE && swapchainDesc) {
        if (!createSwapchain(*swapchainDesc)) {
            std::cerr << "Failed to create swapchain" << std::endl;
            return false;
        }
    }

    updateDeviceProperties();
    return true;
}

bool VulkanDevice::initializeVulkanWin32(
    bool enableValidation,
    void* hinstance,
    void* hwnd,
    const SwapchainDesc* swapchainDesc
) {
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    if (!createInstance(enableValidation, extensions)) {
        std::cerr << "Failed to create Vulkan instance" << std::endl;
        return false;
    }

    if (enableValidation) setupDebugMessenger();

    if (hinstance && hwnd && swapchainDesc) {
        if (!createWin32Surface(hinstance, hwnd)) {
            std::cerr << "Failed to create Win32 surface" << std::endl;
            return false;
        }
    }

    if (!selectPhysicalDevice()) {
        std::cerr << "Failed to select physical device" << std::endl;
        return false;
    }

    if (!createLogicalDevice()) {
        std::cerr << "Failed to create logical device" << std::endl;
        return false;
    }

    if (!createAllocator()) {
        std::cerr << "Failed to create memory allocator" << std::endl;
        return false;
    }

    if (!createCommandPool()) {
        std::cerr << "Failed to create command pool" << std::endl;
        return false;
    }

    if (!createDescriptorPool()) {
        std::cerr << "Failed to create descriptor pool" << std::endl;
        return false;
    }

    if (surface != VK_NULL_HANDLE && swapchainDesc) {
        if (!createSwapchain(*swapchainDesc)) {
            std::cerr << "Failed to create swapchain" << std::endl;
            return false;
        }
    }

    updateDeviceProperties();
    return true;
}

bool VulkanDevice::createInstance(bool enableValidation, const std::vector<const char*>& requiredExtensions) {
    std::vector<const char*> layers;
    if (enableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char*> extensions = requiredExtensions;
    if (surface != VK_NULL_HANDLE) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        #if defined(VK_USE_PLATFORM_WIN32_KHR)
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        #elif defined(VK_USE_PLATFORM_XLIB_KHR)
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        #elif defined(VK_USE_PLATFORM_XCBB_KHR)
        extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
        #elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        #endif
    }
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef __APPLE__
    // MoltenVK portability support
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "XNBY Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "XNBY";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    // Capture validation diagnostics emitted during vkCreateInstance itself.
    VkDebugUtilsMessengerCreateInfoEXT creationDebug{};
    if (enableValidation) {
      creationDebug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      creationDebug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      creationDebug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      creationDebug.pfnUserCallback = debugCallback;
      createInfo.pNext = &creationDebug;
    }
#ifdef __APPLE__
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed with error code: " << result << std::endl;
        std::cerr << "Extensions requested:" << std::endl;
        for (uint32_t i = 0; i < createInfo.enabledExtensionCount; ++i) {
            std::cerr << "  " << createInfo.ppEnabledExtensionNames[i] << std::endl;
        }
        std::cerr << "Layers requested:" << std::endl;
        for (uint32_t i = 0; i < createInfo.enabledLayerCount; ++i) {
            std::cerr << "  " << createInfo.ppEnabledLayerNames[i] << std::endl;
        }
        return false;
    }

    return true;
}

void VulkanDevice::setupDebugMessenger() {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!func) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    func(instance, &createInfo, nullptr, &debugMessenger);
}

bool VulkanDevice::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "No physical devices found" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Prefer discrete GPU, then integrated, then others
    VkPhysicalDevice selected = nullptr;
    int selectedScore = -1;

    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 100;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 50;
        else score = 10;

        // Check for required queue families and extensions
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        bool hasGraphics = false;
        for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                hasGraphics = true;
                properties.graphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (!hasGraphics) continue;

        // Check for required extensions
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, extensions.data());

        bool hasSwapchainExt = false;
        bool hasMemoryBudgetExt = false;
        for (const auto& ext : extensions) {
            if (std::string(ext.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                hasSwapchainExt = true;
            }
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
            if (std::string(ext.extensionName) == VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) {
                hasMemoryBudgetExt = true;
            }
#endif
        }

        if (surface != VK_NULL_HANDLE && !hasSwapchainExt) continue;

        if (score > selectedScore) {
            selected = dev;
            selectedScore = score;
            physicalDevice = dev;
            memoryBudgetSupported = hasMemoryBudgetExt;
        }
    }

    if (!selected) {
        std::cerr << "No suitable physical device found" << std::endl;
        return false;
    }

    physicalDevice = selected;
    vkGetPhysicalDeviceProperties(physicalDevice, &vkProperties);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    std::cout << "Selected GPU: " << vkProperties.deviceName << std::endl;
    return true;
}

bool VulkanDevice::createLogicalDevice() {
    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    properties.graphicsQueueFamilyIndex = 0;
    properties.computeQueueFamilyIndex = 0;
    properties.transferQueueFamilyIndex = 0;

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            properties.graphicsQueueFamilyIndex = i;
        }
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            properties.computeQueueFamilyIndex = i;
        }
        if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            properties.transferQueueFamilyIndex = i;
        }
    }

    std::set<uint32_t> uniqueQueueFamilies = {
        properties.graphicsQueueFamilyIndex,
        properties.computeQueueFamilyIndex,
        properties.transferQueueFamilyIndex
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
    deviceFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;

    std::vector<const char*> extensions;
    if (surface != VK_NULL_HANDLE) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
    if (memoryBudgetSupported) {
        extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
#endif
#ifdef __APPLE__
    // Check if portability subset is available and add it
    {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availExts(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availExts.data());
        for (const auto& ext : availExts) {
            if (std::string(ext.extensionName) == "VK_KHR_portability_subset") {
                extensions.push_back("VK_KHR_portability_subset");
                break;
            }
        }
    }
#endif

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(device, properties.graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, properties.computeQueueFamilyIndex, 0, &computeQueue);
    vkGetDeviceQueue(device, properties.transferQueueFamilyIndex, 0, &transferQueue);

    return true;
}

bool VulkanDevice::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool VulkanDevice::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = properties.graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool VulkanDevice::createDescriptorPool() {
  std::vector<VkDescriptorPoolSize> poolSizes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kDescriptorPoolCapacities[0]},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
      // 每个 PBR 材质 3 个 —— 64 只够 21 个材质,大模型(实测 5k+ 材质的
      // 城市 glb)瞬间耗尽 → createDescriptorSet 返回空 → 绑空集在
      // MoltenVK bindMetalResources 段错误。主池给足,外加下面的溢出池链。
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorPoolCapacities[1]},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kDescriptorPoolCapacities[2]},
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = kDescriptorPoolSetCapacity;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    return false;
  }

  descriptorLifetime->device = device;
  descriptorLifetime->info.available = true;
  descriptorLifetime->info.poolCount = 1;
  descriptorLifetime->usage.emplace(descriptorPool, DescriptorPoolLifetime::Usage{});

  return true;
}

bool VulkanDevice::createSurface(void* windowHandle) {
  // Use SDL to create the Vulkan surface
  auto sdlWindow = static_cast<SDL_Window*>(windowHandle);
  if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, &surface)) {
    std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << std::endl;
    return false;
  }
  return true;
}

bool VulkanDevice::createWin32Surface(void* hinstance, void* hwnd) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = static_cast<HINSTANCE>(hinstance);
    createInfo.hwnd = static_cast<HWND>(hwnd);

    auto createSurfaceFn = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!createSurfaceFn) {
        std::cerr << "vkCreateWin32SurfaceKHR not available" << std::endl;
        return false;
    }

    VkResult result = createSurfaceFn(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        std::cerr << "vkCreateWin32SurfaceKHR failed with error code: " << result << std::endl;
        return false;
    }
    return true;
#else
    (void)hinstance;
    (void)hwnd;
    std::cerr << "Win32 Vulkan surface support was not compiled in" << std::endl;
    return false;
#endif
}

bool VulkanDevice::createSwapchain(const SwapchainDesc& desc) {
    if (surface == VK_NULL_HANDLE) return false;

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_R8G8B8A8_SRGB || format.format == VK_FORMAT_B8G8R8A8_SRGB) &&
            format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            selectedFormat = format;
            break;
        }
    }

    VkPresentModeKHR selectedMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!desc.vsync) {
        for (const auto& mode : presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                selectedMode = mode;
                break;
            }
        }
        if (selectedMode == VK_PRESENT_MODE_FIFO_KHR) {
            for (const auto& mode : presentModes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    selectedMode = mode;
                    break;
                }
            }
        }
    }

    Extent2D extent = desc.extent;
    if (capabilities.currentExtent.width != UINT32_MAX)
      extent = {capabilities.currentExtent.width, capabilities.currentExtent.height};
    if (extent.width < capabilities.minImageExtent.width)
      extent.width = capabilities.minImageExtent.width;
    if (extent.height < capabilities.minImageExtent.height)
      extent.height = capabilities.minImageExtent.height;
    if (extent.width > capabilities.maxImageExtent.width)
      extent.width = capabilities.maxImageExtent.width;
    if (extent.height > capabilities.maxImageExtent.height)
      extent.height = capabilities.maxImageExtent.height;

    uint32_t imageCount = desc.imageCount;
    if (imageCount < capabilities.minImageCount) imageCount = capabilities.minImageCount;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = selectedFormat.format;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent = {extent.width, extent.height};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = selectedMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        return false;
    }

    swapchainExtent = extent;
    swapchainFormat = fromVkFormat(selectedFormat.format);
    swapchainVkFormat = selectedFormat.format;  // preserve exact format for readback
    presentMode = selectedMode;
    vsyncEnabled = desc.vsync;

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr);
    std::vector<VkImage> vkImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, vkImages.data());

    for (VkImage vkImage : vkImages) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = vkImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = selectedFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            return false;
        }

        Extent3D extent3d{swapchainExtent.width, swapchainExtent.height, 1};
        auto texture = std::make_shared<VulkanTexture>(
            vkImage, imageView, nullptr, extent3d, swapchainFormat,
            static_cast<TextureUsageFlags>(TextureUsageFlagBits::ColorAttachment), 1, 1, device, allocator
        );
        swapchainImages.push_back(texture);
    }

    return true;
}

void VulkanDevice::cleanupSwapchain() {
    swapchainImages.clear();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanDevice::updateDeviceProperties() {
    properties.apiVersion = vkProperties.apiVersion;
    properties.vendorID = vkProperties.vendorID;
    properties.deviceID = vkProperties.deviceID;
    std::strncpy(properties.deviceName, vkProperties.deviceName, sizeof(properties.deviceName) - 1);

    switch (vkProperties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            properties.discreteGPU = true;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            properties.integratedGPU = true;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            properties.virtualGPU = true;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            properties.cpuGPU = true;
            break;
        default:
            break;
    }

    properties.maxImageDimension2D = vkProperties.limits.maxImageDimension2D;
    properties.maxImageDimension3D = vkProperties.limits.maxImageDimension3D;
    properties.maxColorAttachments = vkProperties.limits.maxColorAttachments;
}

void VulkanDevice::waitIdle() {
    vkDeviceWaitIdle(device);
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, MemoryPropertyFlags memProps) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & static_cast<VkMemoryPropertyFlags>(memProps))) {
            return i;
        }
    }
    return 0;
}

MemoryBudgetInfo VulkanDevice::getMemoryBudgetInfo() const {
  MemoryBudgetInfo info;
  if (allocator) {
    VmaTotalStatistics totals{};
    vmaCalculateStatistics(allocator, &totals);
    info.allocatorAllocationBytes = totals.total.statistics.allocationBytes;
    info.allocatorBlockBytes = totals.total.statistics.blockBytes;
    info.allocatorAllocationCount = totals.total.statistics.allocationCount;
  }
  if (physicalDevice == VK_NULL_HANDLE) {
    return info;
  }

  VkPhysicalDeviceMemoryProperties2 memoryProperties2{};
  memoryProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
  VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties{};
  if (memoryBudgetSupported) {
    budgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    memoryProperties2.pNext = &budgetProperties;
  }
#endif
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memoryProperties2);

    for (uint32_t heap = 0; heap < memoryProperties2.memoryProperties.memoryHeapCount; ++heap) {
        const VkMemoryHeap& memoryHeap = memoryProperties2.memoryProperties.memoryHeaps[heap];
        if ((memoryHeap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }
        ++info.deviceLocalHeapCount;
        info.deviceLocalHeapBytes += memoryHeap.size;
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
        if (memoryBudgetSupported) {
            info.deviceLocalBudgetBytes += budgetProperties.heapBudget[heap];
            info.deviceLocalUsageBytes += budgetProperties.heapUsage[heap];
        }
#endif
    }
    info.budgetAvailable = memoryBudgetSupported && info.deviceLocalHeapCount > 0 &&
                           info.deviceLocalBudgetBytes > 0;
    return info;
}

RHIBufferPtr VulkanDevice::createBuffer(const BufferDesc& desc) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.memoryProperties & static_cast<MemoryPropertyFlags>(MemoryPropertyFlagBits::HostVisible)) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocInfo.requiredFlags = toVkMemoryPropertyFlags(desc.memoryProperties);
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanBuffer>(buffer, allocation, allocator, desc.size, desc.usage);
}

RHITexturePtr VulkanDevice::createTexture(const TextureDesc& desc) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = desc.type == TextureType::Texture3D ? VK_IMAGE_TYPE_3D :
                          desc.type == TextureType::Texture1D ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D;
    imageInfo.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = toVkFormat(desc.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
    // Cube views require the image to be CUBE_COMPATIBLE (VUID-...-image-01003).
    // The IBL env cubes create a CUBE view below but the flag was missing.
    if (desc.type == TextureType::TextureCube)
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    uint32_t extentMipLevels = 1;
    for (auto n = std::max({desc.extent.width, desc.extent.height, desc.extent.depth}); n > 1; n /= 2)
      ++extentMipLevels;
    if (desc.mipLevels > extentMipLevels || !desc.sampleCount || desc.sampleCount > 64 ||
        (desc.sampleCount & (desc.sampleCount - 1))) return nullptr;

    // Query the actual format/type/tiling/usage combination before driver allocation.
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
    if (!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevels || !desc.arrayLayers ||
        !imageInfo.usage || imageInfo.format == VK_FORMAT_UNDEFINED) return nullptr;
    if (!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevels || !desc.arrayLayers ||
        !imageInfo.usage || imageInfo.format == VK_FORMAT_UNDEFINED) return nullptr;
    VkImageFormatProperties allowed{};
    const VkResult support = vkGetPhysicalDeviceImageFormatProperties(
        physicalDevice, imageInfo.format, imageInfo.imageType, imageInfo.tiling,
        imageInfo.usage, imageInfo.flags, &allowed);
    if (!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevels || !desc.arrayLayers ||
        !imageInfo.usage || imageInfo.format == VK_FORMAT_UNDEFINED ||
        (isBlockCompressedFormat(desc.format) && !supportedFeatures.textureCompressionBC) ||
        support != VK_SUCCESS || desc.extent.width > allowed.maxExtent.width ||
        desc.extent.height > allowed.maxExtent.height || desc.extent.depth > allowed.maxExtent.depth ||
        desc.mipLevels > allowed.maxMipLevels || desc.arrayLayers > allowed.maxArrayLayers ||
        !(allowed.sampleCounts & imageInfo.samples)) {
      std::cerr << "createTexture: unsupported format, extent, mip, layer or sample configuration" << std::endl;
      return nullptr;
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = desc.type == TextureType::TextureCube ? VK_IMAGE_VIEW_TYPE_CUBE :
                       desc.type == TextureType::Texture3D ? VK_IMAGE_VIEW_TYPE_3D :
                       desc.type == TextureType::Texture1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = (desc.usage & TextureUsageFlagBits::DepthStencilAttachment)
        ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        vmaDestroyImage(allocator, image, allocation);
        return nullptr;
    }

    return std::make_shared<VulkanTexture>(image,
                                           imageView,
                                           allocation,
                                           desc.extent,
                                           desc.format,
                                           desc.usage,
                                           desc.mipLevels,
                                           desc.arrayLayers,
                                           device,
                                           allocator);
}

RHIShaderPtr VulkanDevice::createShader(const ShaderDesc& desc) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = desc.spirvCode.size() * sizeof(uint32_t);
    createInfo.pCode = desc.spirvCode.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanShader>(shaderModule, desc.stage);
}

RHIRenderPassPtr VulkanDevice::createRenderPass(const RenderPassDesc& desc) {
    std::vector<VkAttachmentDescription> attachments;
    for (const auto& att : desc.attachments) {
        VkAttachmentDescription vkAtt{};
        vkAtt.format = toVkFormat(att.format);
        vkAtt.samples = static_cast<VkSampleCountFlagBits>(att.sampleCount);
        vkAtt.loadOp = att.loadOpClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        vkAtt.storeOp = att.storeOpStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vkAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        vkAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vkAtt.initialLayout = toVkImageLayout(att.initialLayout);
        vkAtt.finalLayout = toVkImageLayout(att.finalLayout);
        attachments.push_back(vkAtt);
    }

    std::vector<VkAttachmentReference> colorAttachmentRefs;
    std::vector<std::vector<VkAttachmentReference>> subpassColorAttachments;
    VkAttachmentReference depthAttachmentRef;
    std::vector<bool> hasDepthStencil;

    for (const auto& subpass : desc.subpasses) {
        std::vector<VkAttachmentReference> colorRefs;
        for (const auto& ref : subpass.colorAttachments) {
            VkAttachmentReference vkRef{};
            vkRef.attachment = ref.attachment;
            vkRef.layout = toVkImageLayout(ref.layout);
            colorRefs.push_back(vkRef);
        }
        subpassColorAttachments.push_back(colorRefs);
        hasDepthStencil.push_back(subpass.hasDepthStencil);

        if (subpass.hasDepthStencil) {
            depthAttachmentRef.attachment = subpass.depthStencilAttachment.attachment;
            depthAttachmentRef.layout = toVkImageLayout(subpass.depthStencilAttachment.layout);
        }
    }

    std::vector<VkSubpassDescription> subpasses;
    for (size_t i = 0; i < desc.subpasses.size(); ++i) {
        VkSubpassDescription vkSubpass{};
        vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        vkSubpass.colorAttachmentCount = subpassColorAttachments[i].size();
        vkSubpass.pColorAttachments = subpassColorAttachments[i].data();
        if (hasDepthStencil[i]) {
            vkSubpass.pDepthStencilAttachment = &depthAttachmentRef;
        }
        subpasses.push_back(vkSubpass);
    }

    std::vector<VkSubpassDependency> dependencies;
    for (const auto& dep : desc.dependencies) {
        VkSubpassDependency vkDep{};
        vkDep.srcSubpass = dep.srcSubpass;
        vkDep.dstSubpass = dep.dstSubpass;
        vkDep.srcStageMask = toVkPipelineStageFlags(dep.srcStageMask);
        vkDep.dstStageMask = toVkPipelineStageFlags(dep.dstStageMask);
        vkDep.srcAccessMask = toVkAccessFlags(dep.srcAccessMask);
        vkDep.dstAccessMask = toVkAccessFlags(dep.dstAccessMask);
        vkDep.dependencyFlags = dep.byRegion ? VK_DEPENDENCY_BY_REGION_BIT : 0;
        dependencies.push_back(vkDep);
    }

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = attachments.size();
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = subpasses.size();
    createInfo.pSubpasses = subpasses.data();
    createInfo.dependencyCount = dependencies.size();
    createInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanRenderPass>(renderPass, attachments.size());
}

RHIFramebufferPtr VulkanDevice::createFramebuffer(
    const RHIRenderPassPtr& renderPass,
    const std::vector<RHITextureViewPtr>& attachments,
    const Extent2D& extent,
    uint32_t layers
) {
    auto vkRenderPass = std::static_pointer_cast<VulkanRenderPass>(renderPass);
    std::vector<VkImageView> imageViews;

    for (const auto& att : attachments) {
        auto vkView = std::static_pointer_cast<VulkanTextureView>(att);
        imageViews.push_back(vkView->getVkImageView());
    }

    VkFramebufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.renderPass = vkRenderPass->getVkRenderPass();
    createInfo.attachmentCount = imageViews.size();
    createInfo.pAttachments = imageViews.data();
    createInfo.width = extent.width;
    createInfo.height = extent.height;
    createInfo.layers = layers;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanFramebuffer>(framebuffer, extent, layers);
}

RHISamplerPtr VulkanDevice::createSampler(const SamplerDesc& desc) {
    VkSamplerCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = toVkFilter(desc.magFilter);
    createInfo.minFilter = toVkFilter(desc.minFilter);
    createInfo.mipmapMode = toVkMipmapMode(desc.mipmapMode);
    createInfo.addressModeU = toVkAddressMode(desc.addressModeU);
    createInfo.addressModeV = toVkAddressMode(desc.addressModeV);
    createInfo.addressModeW = toVkAddressMode(desc.addressModeW);
    createInfo.mipLodBias = desc.mipLodBias;
    createInfo.anisotropyEnable = desc.anisotropyEnable;
    createInfo.maxAnisotropy = desc.maxAnisotropy;
    createInfo.compareEnable = desc.compareEnable;
    createInfo.compareOp = toVkCompareOp(desc.compareOp);
    createInfo.minLod = desc.minLod;
    createInfo.maxLod = desc.maxLod;
    createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    createInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler;
    if (vkCreateSampler(device, &createInfo, nullptr, &sampler) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanSampler>(sampler);
}

RHIDescriptorSetLayoutPtr VulkanDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& binding : desc.bindings) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.descriptorCount = binding.descriptorCount;
        switch (binding.descriptorType) {
            case DescriptorType::CombinedImageSampler:
                vkBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
            case DescriptorType::StorageBuffer:
                vkBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
            case DescriptorType::UniformBuffer:
            default:
                vkBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
        }
        VkShaderStageFlags stages = 0;
        if (binding.stageFlags & static_cast<uint32_t>(ShaderStageFlagBits::Vertex))
            stages |= VK_SHADER_STAGE_VERTEX_BIT;
        if (binding.stageFlags & static_cast<uint32_t>(ShaderStageFlagBits::Fragment))
            stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (binding.stageFlags & static_cast<uint32_t>(ShaderStageFlagBits::Compute))
            stages |= VK_SHADER_STAGE_COMPUTE_BIT;
        vkBinding.stageFlags = stages ? stages
            : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
        bindings.push_back(vkBinding);
    }

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &layout) != VK_SUCCESS) {
      return nullptr;
    }
    DescriptorCounts counts{};
    for (const auto& binding : bindings) {
      const size_t index = binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? 1
                           : binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER       ? 2
                                                                                               : 0;
      if (binding.descriptorCount > kDescriptorPoolCapacities[index] - counts[index]) {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
        return nullptr;
      }
      counts[index] += binding.descriptorCount;
    }
    return std::make_shared<VulkanDescriptorSetLayout>(layout, counts);
}

VkDescriptorPool VulkanDevice::allocateOverflowPool() {
  std::vector<VkDescriptorPoolSize> poolSizes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kDescriptorPoolCapacities[0]},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorPoolCapacities[1]},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kDescriptorPoolCapacities[2]},
  };
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = kDescriptorPoolSetCapacity;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  overflowPools.push_back(pool);
  descriptorLifetime->usage.emplace(pool, DescriptorPoolLifetime::Usage{});
  ++descriptorLifetime->info.poolCount;
  std::cerr << "VulkanDevice: descriptor pool exhausted — grew overflow pool #" << overflowPools.size() << std::endl;
  return pool;
}

RHIDescriptorSetPtr VulkanDevice::createDescriptorSet(const RHIDescriptorSetLayoutPtr& layout,
                                                      const DescriptorSetDesc& desc) {
  if (!layout)
    return nullptr;
  auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(layout);

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  VkDescriptorSetLayout vkSetLayout = vkLayout->getVkLayout();
  allocInfo.pSetLayouts = &vkSetLayout;

  VkDescriptorSet descriptorSet;
  std::unique_lock allocationLock(descriptorLifetime->mutex);
  const auto& counts = vkLayout->getCounts();
  const auto allocate = [&](VkDescriptorPool pool) {
    const auto& used = descriptorLifetime->usage.at(pool);
    if (used.sets >= kDescriptorPoolSetCapacity)
      return VK_ERROR_OUT_OF_POOL_MEMORY;
    for (size_t i = 0; i < counts.size(); ++i)
      if (counts[i] > kDescriptorPoolCapacities[i] - used.descriptors[i])
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    allocInfo.descriptorPool = pool;
    return vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
  };
  VkResult ar = allocate(descriptorPool);
  if (ar == VK_ERROR_OUT_OF_POOL_MEMORY || ar == VK_ERROR_FRAGMENTED_POOL) {
    // 先试已有溢出池,再开新池 —— 大场景按需增长,永不因池满返回空。
    ar = VK_ERROR_OUT_OF_POOL_MEMORY;
    for (VkDescriptorPool pool : overflowPools) {
      ar = allocate(pool);
      if (ar == VK_SUCCESS)
        break;
    }
    if (ar != VK_SUCCESS) {
      VkDescriptorPool fresh = allocateOverflowPool();
      if (fresh != VK_NULL_HANDLE) {
        ar = allocate(fresh);
      }
    }
  }
  if (ar != VK_SUCCESS) {
    std::cerr << "createDescriptorSet: allocation failed (" << ar << ")" << std::endl;
    return nullptr;
  }
  ++descriptorLifetime->info.allocatedSets;
  ++descriptorLifetime->info.liveSets;
  auto& used = descriptorLifetime->usage.at(allocInfo.descriptorPool);
  ++used.sets;
  for (size_t i = 0; i < counts.size(); ++i)
    used.descriptors[i] += counts[i];
  descriptorLifetime->info.peakLiveSets =
      std::max(descriptorLifetime->info.peakLiveSets, descriptorLifetime->info.liveSets);
  std::shared_ptr<VulkanDescriptorSet> ownedSet;
  try {
    ownedSet =
        std::make_shared<VulkanDescriptorSet>(descriptorSet, allocInfo.descriptorPool, counts, descriptorLifetime);
  } catch (...) {
    if (vkFreeDescriptorSets(device, allocInfo.descriptorPool, 1, &descriptorSet) == VK_SUCCESS) {
      ++descriptorLifetime->info.freedSets;
      --descriptorLifetime->info.liveSets;
      --used.sets;
      for (size_t i = 0; i < counts.size(); ++i)
        used.descriptors[i] -= counts[i];
    } else {
      ++descriptorLifetime->info.freeFailures;
    }
    throw;
  }
  allocationLock.unlock();

  // Write bindings — 三种描述符:CombinedImageSampler / StorageBuffer / UniformBuffer
  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorImageInfo> imageInfos;
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  imageInfos.reserve(desc.bindings.size());
  bufferInfos.reserve(desc.bindings.size());

  // 先把 imageInfo / bufferInfo 全部 push 进 vector 占位置(因为 VkWriteDescriptorSet
  // 持有 pImageInfo / pBufferInfo 指针,reserve 不够,要等所有 push 完再回填指针)
  struct Pending {
    VkWriteDescriptorSet write;
    int imageIdx = -1;
    int bufferIdx = -1;
  };
  std::vector<Pending> pendings;

  for (const auto& binding : desc.bindings) {
    Pending pending;
    VkWriteDescriptorSet& write = pending.write;
    write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet;
    write.dstBinding = binding.binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;

    if (binding.type == DescriptorType::CombinedImageSampler && binding.textureView && binding.sampler) {
      auto vkView = std::static_pointer_cast<VulkanTextureView>(binding.textureView);
      auto vkSampler = std::static_pointer_cast<VulkanSampler>(binding.sampler);
      VkDescriptorImageInfo imgInfo{};
      imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imgInfo.imageView = vkView->getVkImageView();
      imgInfo.sampler = vkSampler->getVkSampler();
      pending.imageIdx = static_cast<int>(imageInfos.size());
      imageInfos.push_back(imgInfo);
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    } else if ((binding.type == DescriptorType::StorageBuffer || binding.type == DescriptorType::UniformBuffer) &&
               binding.buffer) {
      auto vkBuf = std::static_pointer_cast<VulkanBuffer>(binding.buffer);
      VkDescriptorBufferInfo bufInfo{};
      bufInfo.buffer = vkBuf->getVkBuffer();
      bufInfo.offset = binding.bufferOffset;
      bufInfo.range = binding.bufferRange == 0 ? VK_WHOLE_SIZE : binding.bufferRange;
      pending.bufferIdx = static_cast<int>(bufferInfos.size());
      bufferInfos.push_back(bufInfo);
      write.descriptorType = (binding.type == DescriptorType::StorageBuffer) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    } else {
      // 描述符布局里声明的 binding 给了空资源:跳过写入会留下
      // 未初始化描述符,绑定绘制时在 MoltenVK encode 段错误
      // (城市级 glb 导入崩溃的根源之一)。整集判失败,调用方
      // 走 fallback 材质 / 跳过绘制。
      std::cerr << "createDescriptorSet: binding " << binding.binding << " has null resource — set rejected"
                << std::endl;
      return nullptr;
    }
    pendings.push_back(pending);
  }
  // 回填指针(vector 已经稳定,不再 push_back)
  for (auto& p : pendings) {
    if (p.imageIdx >= 0)
      p.write.pImageInfo = &imageInfos[p.imageIdx];
    if (p.bufferIdx >= 0)
      p.write.pBufferInfo = &bufferInfos[p.bufferIdx];
    writes.push_back(p.write);
  }
  if (!writes.empty()) {
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  return ownedSet;
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
  std::lock_guard lock(lifetime->mutex);
  if (lifetime->device == VK_NULL_HANDLE)
    return;
  if (vkFreeDescriptorSets(lifetime->device, pool, 1, &descriptorSet) == VK_SUCCESS) {
    ++lifetime->info.freedSets;
    --lifetime->info.liveSets;
    auto& used = lifetime->usage.at(pool);
    --used.sets;
    for (size_t i = 0; i < counts.size(); ++i) used.descriptors[i] -= counts[i];
  } else {
    ++lifetime->info.freeFailures;
  }
}

DescriptorAllocationInfo VulkanDevice::getDescriptorAllocationInfo() const {
  std::lock_guard lock(descriptorLifetime->mutex);
  return descriptorLifetime->info;
}

RHIPipelineLayoutPtr VulkanDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
  std::vector<VkDescriptorSetLayout> layouts;
  for (const auto& layout : desc.setLayouts) {
    if (!layout) {
      std::cerr << "VulkanDevice::createPipelineLayout: null descriptor set layout in setLayouts" << std::endl;
      return nullptr;
    }
    auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(layout);
    layouts.push_back(vkLayout->getVkLayout());
  }

  std::vector<VkPushConstantRange> ranges;
  for (const auto& range : desc.pushConstantRanges) {
    VkPushConstantRange vkRange{};
    // Honor the caller's stage mask instead of forcing V|F. Forcing V|F
    // made fragment-only / vertex-only passes (compose, ssao, bloom,
    // particles, billboards) violate VUID-vkCmdPushConstants-offset-01796:
    // they push a single stage while the layout claimed V|F, leaving the
    // un-pushed stage's push-constant bytes UNDEFINED. Most passes don't
    // read the missing stage so it was latent, but it's spec-illegal and
    // undefined-memory behaviour on some drivers.
    VkShaderStageFlags stages = 0;
    const uint32_t sf = static_cast<uint32_t>(range.stageFlags);
    if (sf & static_cast<uint32_t>(ShaderStageFlagBits::Vertex))
      stages |= VK_SHADER_STAGE_VERTEX_BIT;
    if (sf & static_cast<uint32_t>(ShaderStageFlagBits::Fragment))
      stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (sf & static_cast<uint32_t>(ShaderStageFlagBits::Geometry))
      stages |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (sf & static_cast<uint32_t>(ShaderStageFlagBits::Compute))
      stages |= VK_SHADER_STAGE_COMPUTE_BIT;
    // Fallback: a range with no recognised stage would be invalid; keep
    // the historical V|F so old call sites that left stageFlags 0 still work.
    vkRange.stageFlags = stages ? stages : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    vkRange.offset = range.offset;
    vkRange.size = range.size;
    ranges.push_back(vkRange);
  }

  VkPipelineLayoutCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  createInfo.setLayoutCount = layouts.size();
  createInfo.pSetLayouts = layouts.data();
  createInfo.pushConstantRangeCount = ranges.size();
  createInfo.pPushConstantRanges = ranges.data();

  VkPipelineLayout layout;
  if (vkCreatePipelineLayout(device, &createInfo, nullptr, &layout) != VK_SUCCESS) {
    return nullptr;
  }

  return std::make_shared<VulkanPipelineLayout>(layout);
}

RHIPipelinePtr VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    for (const auto& shader : desc.shaders) {
        auto vkShader = std::static_pointer_cast<VulkanShader>(shader);
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = toVkShaderStage(vkShader->getStage());
        stageInfo.module = vkShader->getVkShaderModule();
        stageInfo.pName = "main";
        shaderStages.push_back(stageInfo);
    }

    // Vertex input
    std::vector<VkVertexInputBindingDescription> vkBindings;
    for (const auto& b : desc.vertexInput.bindings) {
        VkVertexInputBindingDescription vkB{};
        vkB.binding = b.binding;
        vkB.stride = b.stride;
        vkB.inputRate = b.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        vkBindings.push_back(vkB);
    }
    std::vector<VkVertexInputAttributeDescription> vkAttrs;
    for (const auto& a : desc.vertexInput.attributes) {
        VkVertexInputAttributeDescription vkA{};
        vkA.location = a.location;
        vkA.binding = a.binding;
        vkA.format = toVkFormat(a.format);
        vkA.offset = a.offset;
        vkAttrs.push_back(vkA);
    }
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vkBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttrs.size());
    vertexInputInfo.pVertexAttributeDescriptions = vkAttrs.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkPrimitiveTopology(desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

    // Dynamic state (viewport and scissor)
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Viewport state (counts only, actual values set dynamically)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = desc.rasterization.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
    rasterizer.polygonMode = toVkFillMode(desc.rasterization.fillMode);
    rasterizer.cullMode = toVkCullMode(desc.rasterization.cullMode);
    rasterizer.frontFace = desc.rasterization.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.rasterization.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.lineWidth = desc.rasterization.lineWidth;

    // Multisampling (no MSAA)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompareOp);

    // Color blending
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    if (desc.colorBlendAttachments.empty()) {
        // Default: no blending, write all channels
        VkPipelineColorBlendAttachmentState defaultBlend{};
        defaultBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        defaultBlend.blendEnable = VK_FALSE;
        blendAttachments.push_back(defaultBlend);
    } else {
        for (const auto& att : desc.colorBlendAttachments) {
            VkPipelineColorBlendAttachmentState vkAtt{};
            vkAtt.blendEnable = att.blendEnable ? VK_TRUE : VK_FALSE;
            vkAtt.srcColorBlendFactor = toVkBlendFactor(att.srcColorBlendFactor);
            vkAtt.dstColorBlendFactor = toVkBlendFactor(att.dstColorBlendFactor);
            vkAtt.colorBlendOp = toVkBlendOp(att.colorBlendOp);
            vkAtt.srcAlphaBlendFactor = toVkBlendFactor(att.srcAlphaBlendFactor);
            vkAtt.dstAlphaBlendFactor = toVkBlendFactor(att.dstAlphaBlendFactor);
            vkAtt.alphaBlendOp = toVkBlendOp(att.alphaBlendOp);
            vkAtt.colorWriteMask = att.colorWriteMask;
            blendAttachments.push_back(vkAtt);
        }
    }
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    // Pipeline layout
    VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
    if (desc.pipelineLayout) {
        auto vkLayout = std::static_pointer_cast<VulkanPipelineLayout>(desc.pipelineLayout);
        vkPipelineLayout = vkLayout->getVkLayout();
    } else {
        // Create an empty pipeline layout
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &vkPipelineLayout);
    }

    // Render pass
    VkRenderPass vkRenderPass = VK_NULL_HANDLE;
    if (desc.renderPass) {
        auto rp = std::static_pointer_cast<VulkanRenderPass>(desc.renderPass);
        vkRenderPass = rp->getVkRenderPass();
    }

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = vkPipelineLayout;
    pipelineInfo.renderPass = vkRenderPass;
    pipelineInfo.subpass = desc.subpass;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        std::cerr << "vkCreateGraphicsPipelines failed: " << result << std::endl;
        return nullptr;
    }

    return std::make_shared<VulkanPipeline>(pipeline, PipelineBindPoint::Graphics);
}

RHIPipelinePtr VulkanDevice::createComputePipeline(const ComputePipelineDesc& desc) {
    if (!desc.shader || !desc.pipelineLayout) {
        std::cerr << "createComputePipeline: shader or pipelineLayout is null\n";
        return nullptr;
    }
    auto vkShader = std::static_pointer_cast<VulkanShader>(desc.shader);
    auto vkLayout = std::static_pointer_cast<VulkanPipelineLayout>(desc.pipelineLayout);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = vkShader->getVkShaderModule();
    stage.pName  = "main";

    VkComputePipelineCreateInfo createInfo{};
    createInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.stage  = stage;
    createInfo.layout = vkLayout->getVkLayout();

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "vkCreateComputePipelines failed\n";
        return nullptr;
    }
    return std::make_shared<VulkanPipeline>(pipeline, PipelineBindPoint::Compute);
}

RHICommandBufferPtr VulkanDevice::createCommandBuffer(bool primary) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanCommandBuffer>(cmdBuffer, device);
}

RHISemaphorePtr VulkanDevice::createSemaphore() {
    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore semaphore;
    if (vkCreateSemaphore(device, &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanSemaphore>(semaphore);
}

RHIFencePtr VulkanDevice::createFence(bool signaled) {
    VkFenceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    VkFence fence;
    if (vkCreateFence(device, &createInfo, nullptr, &fence) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanFence>(fence, device);
}

RHIEventPtr VulkanDevice::createEvent() {
    VkEventCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;

    VkEvent event;
    if (vkCreateEvent(device, &createInfo, nullptr, &event) != VK_SUCCESS) {
        return nullptr;
    }

    return std::make_shared<VulkanEvent>(event, device);
}

void VulkanDevice::submit(
    const std::vector<RHICommandBufferPtr>& commandBuffers,
    const std::vector<RHISemaphorePtr>& waitSemaphores,
    const std::vector<RHISemaphorePtr>& signalSemaphores,
    const RHIFencePtr& fence
) {
    std::vector<VkCommandBuffer> vkCmdBuffers;
    for (const auto& cmd : commandBuffers) {
        auto vkCmd = std::static_pointer_cast<VulkanCommandBuffer>(cmd);
        vkCmdBuffers.push_back(vkCmd->getVkCommandBuffer());
    }

    std::vector<VkSemaphore> vkWaitSemaphores;
    for (const auto& sem : waitSemaphores) {
        auto vkSem = std::static_pointer_cast<VulkanSemaphore>(sem);
        vkWaitSemaphores.push_back(vkSem->getVkSemaphore());
    }

    std::vector<VkSemaphore> vkSignalSemaphores;
    for (const auto& sem : signalSemaphores) {
        auto vkSem = std::static_pointer_cast<VulkanSemaphore>(sem);
        vkSignalSemaphores.push_back(vkSem->getVkSemaphore());
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::vector<VkPipelineStageFlags> waitStages(vkWaitSemaphores.size(), waitStage);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSemaphores.size());
    submitInfo.pWaitSemaphores = vkWaitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = static_cast<uint32_t>(vkCmdBuffers.size());
    submitInfo.pCommandBuffers = vkCmdBuffers.data();
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(vkSignalSemaphores.size());
    submitInfo.pSignalSemaphores = vkSignalSemaphores.data();

    VkFence vkFence = VK_NULL_HANDLE;
    if (fence) {
        auto vkF = std::static_pointer_cast<VulkanFence>(fence);
        vkFence = vkF->getVkFence();
    }

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, vkFence);
}

void VulkanDevice::present(const RHISemaphorePtr& waitSemaphore) {
    if (swapchain == VK_NULL_HANDLE) return;

    VkSemaphore sem = VK_NULL_HANDLE;
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &currentSwapchainImageIndex;

    if (waitSemaphore) {
        auto vkSem = std::static_pointer_cast<VulkanSemaphore>(waitSemaphore);
        sem = vkSem->getVkSemaphore();
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &sem;
    }

    vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

uint32_t VulkanDevice::acquireNextImage(const RHISemaphorePtr& semaphore, const RHIFencePtr& fence) {
    if (swapchain == VK_NULL_HANDLE) return 0;

    VkSemaphore vkSemaphore = nullptr;
    if (semaphore) {
        auto vkSem = std::static_pointer_cast<VulkanSemaphore>(semaphore);
        vkSemaphore = vkSem->getVkSemaphore();
    }

    uint32_t imageIndex = UINT32_MAX;
    const auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, vkSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return UINT32_MAX;
    currentSwapchainImageIndex = imageIndex;
    return imageIndex;
}

RHITexturePtr VulkanDevice::getSwapchainImage(uint32_t index) {
    if (index >= swapchainImages.size()) return nullptr;
    return swapchainImages[index];
}

bool VulkanDevice::resizeSwapchain(const Extent2D& newExtent) {
  if (!newExtent.width || !newExtent.height || surface == VK_NULL_HANDLE)
    return false;
  waitIdle();
  SwapchainDesc desc;
  desc.extent = newExtent;
  desc.imageCount = static_cast<uint32_t>(swapchainImages.size());
  desc.format = swapchainFormat;
  desc.vsync = vsyncEnabled;
  cleanupSwapchain();
  if (!createSwapchain(desc)) {
    cleanupSwapchain();
    return false;
  }
  return true;
}

void VulkanDevice::recreateSwapchain(const Extent2D& newExtent) {
  resizeSwapchain(newExtent);
}

void VulkanDevice::waitForQueue(uint32_t queueFamily) {
  VkQueue queue;
  if (queueFamily == properties.graphicsQueueFamilyIndex) {
    queue = graphicsQueue;
  } else if (queueFamily == properties.computeQueueFamilyIndex) {
    queue = computeQueue;
  } else {
    queue = transferQueue;
  }
  vkQueueWaitIdle(queue);
}

// Format Conversion Helpers
VkFormat VulkanDevice::toVkFormat(Format format) const {
    static const VkFormat table[] = {
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SNORM, VK_FORMAT_R8_UINT, VK_FORMAT_R8_SINT,
        VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8_SINT,
        VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8_SINT,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_SINT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SNORM, VK_FORMAT_R16_UINT, VK_FORMAT_R16_SINT, VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_BC2_UNORM_BLOCK,
        VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK,
    };
    static_assert(std::size(table) == static_cast<size_t>(Format::Count));
    const auto index = static_cast<size_t>(format);
    return index < std::size(table) ? table[index] : VK_FORMAT_UNDEFINED;
}

Format VulkanDevice::fromVkFormat(VkFormat format) const {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_SRGB: return Format::R8G8B8A8_SRGB; // Map BGRA to RGBA
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM: return Format::R8G8B8A8_UNORM; // Map BGRA to RGBA
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32_SFLOAT;
        case VK_FORMAT_D32_SFLOAT: return Format::D32_SFLOAT;
        default: return Format::Undefined;
    }
}

VkImageUsageFlags VulkanDevice::toVkImageUsage(TextureUsageFlags usage) const {
    VkImageUsageFlags result = 0;
    if (usage & TextureUsageFlagBits::TransferDst) result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & TextureUsageFlagBits::TransferSrc) result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & TextureUsageFlagBits::Sampled) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & TextureUsageFlagBits::Storage) result |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage & TextureUsageFlagBits::ColorAttachment) result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & TextureUsageFlagBits::DepthStencilAttachment) result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return result;
}

VkBufferUsageFlags VulkanDevice::toVkBufferUsage(BufferUsageFlags usage) const {
    VkBufferUsageFlags result = 0;
    if (usage & BufferUsageFlagBits::TransferDst) result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (usage & BufferUsageFlagBits::TransferSrc) result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (usage & BufferUsageFlagBits::Uniform) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (usage & BufferUsageFlagBits::Storage) result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (usage & BufferUsageFlagBits::Index) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usage & BufferUsageFlagBits::Vertex) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usage & BufferUsageFlagBits::Indirect) result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return result;
}

VkShaderStageFlagBits VulkanDevice::toVkShaderStage(ShaderStageFlagBits stage) const {
    switch (stage) {
        case ShaderStageFlagBits::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStageFlagBits::TessellationControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStageFlagBits::TessellationEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderStageFlagBits::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStageFlagBits::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStageFlagBits::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
        default: return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

VkPrimitiveTopology VulkanDevice::toVkPrimitiveTopology(PrimitiveTopology topology) const {
    switch (topology) {
        case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCompareOp VulkanDevice::toVkCompareOp(CompareOp op) const {
    static const VkCompareOp table[] = {
        VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
    };
    return table[static_cast<size_t>(op)];
}

VkBlendFactor VulkanDevice::toVkBlendFactor(BlendFactor factor) const {
    static const VkBlendFactor table[] = {
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
    };
    return table[static_cast<size_t>(factor)];
}

VkBlendOp VulkanDevice::toVkBlendOp(BlendOp op) const {
    static const VkBlendOp table[] = {
        VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX,
    };
    return table[static_cast<size_t>(op)];
}

VkCullModeFlagBits VulkanDevice::toVkCullMode(CullMode mode) const {
    switch (mode) {
        case CullMode::None: return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_BACK_BIT;
    }
}

VkPolygonMode VulkanDevice::toVkFillMode(FillMode mode) const {
    switch (mode) {
        case FillMode::Solid: return VK_POLYGON_MODE_FILL;
        case FillMode::Wireframe: return VK_POLYGON_MODE_LINE;
        case FillMode::Point: return VK_POLYGON_MODE_POINT;
        default: return VK_POLYGON_MODE_FILL;
    }
}

VkImageLayout VulkanDevice::toVkImageLayout(ImageLayout layout) const {
    static const VkImageLayout table[] = {
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PREINITIALIZED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    return table[static_cast<size_t>(layout)];
}

VkAccessFlags VulkanDevice::toVkAccessFlags(AccessFlags flags) const {
    VkAccessFlags result = 0;
    if (flags & AccessFlagBits::IndirectCommandRead) result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (flags & AccessFlagBits::IndexRead) result |= VK_ACCESS_INDEX_READ_BIT;
    if (flags & AccessFlagBits::VertexAttributeRead) result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (flags & AccessFlagBits::UniformRead) result |= VK_ACCESS_UNIFORM_READ_BIT;
    if (flags & AccessFlagBits::ShaderRead) result |= VK_ACCESS_SHADER_READ_BIT;
    if (flags & AccessFlagBits::ShaderWrite) result |= VK_ACCESS_SHADER_WRITE_BIT;
    if (flags & AccessFlagBits::ColorAttachmentRead) result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (flags & AccessFlagBits::ColorAttachmentWrite) result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (flags & AccessFlagBits::DepthStencilAttachmentRead) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (flags & AccessFlagBits::DepthStencilAttachmentWrite) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (flags & AccessFlagBits::TransferRead) result |= VK_ACCESS_TRANSFER_READ_BIT;
    if (flags & AccessFlagBits::TransferWrite) result |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (flags & AccessFlagBits::HostRead) result |= VK_ACCESS_HOST_READ_BIT;
    if (flags & AccessFlagBits::HostWrite) result |= VK_ACCESS_HOST_WRITE_BIT;
    if (flags & AccessFlagBits::MemoryRead) result |= VK_ACCESS_MEMORY_READ_BIT;
    if (flags & AccessFlagBits::MemoryWrite) result |= VK_ACCESS_MEMORY_WRITE_BIT;
    return result;
}

VkPipelineStageFlags VulkanDevice::toVkPipelineStageFlags(PipelineStageFlags flags) const {
    VkPipelineStageFlags result = 0;
    if (flags & PipelineStageFlagBits::TopOfPipe) result |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (flags & PipelineStageFlagBits::VertexInput) result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (flags & PipelineStageFlagBits::VertexShader) result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (flags & PipelineStageFlagBits::TessellationControlShader) result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    if (flags & PipelineStageFlagBits::TessellationEvaluationShader) result |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (flags & PipelineStageFlagBits::GeometryShader) result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (flags & PipelineStageFlagBits::FragmentShader) result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (flags & PipelineStageFlagBits::EarlyFragmentTests) result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    if (flags & PipelineStageFlagBits::LateFragmentTests) result |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    if (flags & PipelineStageFlagBits::ColorAttachmentOutput) result |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (flags & PipelineStageFlagBits::ComputeShader) result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (flags & PipelineStageFlagBits::Transfer) result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (flags & PipelineStageFlagBits::BottomOfPipe) result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (flags & PipelineStageFlagBits::Host) result |= VK_PIPELINE_STAGE_HOST_BIT;
    return result;
}

VkSamplerAddressMode VulkanDevice::toVkAddressMode(SamplerAddressMode mode) const {
    switch (mode) {
        case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case SamplerAddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkFilter VulkanDevice::toVkFilter(SamplerFilter filter) const {
    return filter == SamplerFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerMipmapMode VulkanDevice::toVkMipmapMode(SamplerMipmapMode mode) const {
    return mode == SamplerMipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkMemoryPropertyFlags VulkanDevice::toVkMemoryPropertyFlags(MemoryPropertyFlags flags) const {
    VkMemoryPropertyFlags result = 0;
    if (flags & MemoryPropertyFlagBits::DeviceLocal) result |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (flags & MemoryPropertyFlagBits::HostVisible) result |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (flags & MemoryPropertyFlagBits::HostCoherent) result |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (flags & MemoryPropertyFlagBits::HostCached) result |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    if (flags & MemoryPropertyFlagBits::LazilyAllocated) result |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    return result;
}

// ============================================================================
// uploadTextureData Implementation
// ============================================================================

void VulkanDevice::uploadTextureData(
    const RHITexturePtr& texture,
    const std::vector<MipData>& mips,
    uint32_t arrayLayer
) {
    if (!texture || mips.empty()) return;

    auto vkTexture = std::static_pointer_cast<VulkanTexture>(texture);
    VkImage image = vkTexture->getVkImage();

    // Determine image aspect from format
    Format texFormat = vkTexture->getFormat();
    VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (texFormat == Format::D32_SFLOAT ||
        texFormat == Format::D32_SFLOAT_S8_UINT ||
        texFormat == Format::D24_UNORM_S8_UINT) {
        imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // I3: Bounds check on mip count vs texture mip levels
    if (mips.size() > vkTexture->getMipLevels()) {
        std::cerr << "uploadTextureData: mip count (" << mips.size()
                  << ") exceeds texture mip levels (" << vkTexture->getMipLevels() << ")" << std::endl;
        return;
    }

    // Validate every mip and align copy offsets before any staging allocation.
    const auto layout = planTextureMipUpload(texFormat, vkTexture->getExtent(), vkTexture->getMipLevels(),
                                             vkTexture->getArrayLayers(), arrayLayer, mips);
    const VkDeviceSize totalSize = layout.bytes;

    // 2. Create host-visible staging buffer via VMA
    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = totalSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingAllocResult{};
    if (vmaCreateBuffer(allocator, &stagingBufInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, &stagingAllocResult) != VK_SUCCESS) {
        std::cerr << "uploadTextureData: failed to create staging buffer" << std::endl;
        return;
    }

    // 3. Copy each mip's data into the staging buffer
    uint8_t* mapped = static_cast<uint8_t*>(stagingAllocResult.pMappedData);
    for (size_t i = 0; i < mips.size(); i++) {
        if (!mips[i].data) {
            std::cerr << "uploadTextureData: mips[" << i << "].data is null, aborting" << std::endl;
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return;
        }
        std::memcpy(mapped + layout.slices[i].offset, mips[i].data, mips[i].dataSize);
    }
    vmaFlushAllocation(allocator, stagingAllocation, 0, VK_WHOLE_SIZE);

    // 4. Allocate a one-time command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        std::cerr << "uploadTextureData: failed to allocate command buffer" << std::endl;
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 5. Barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = imageAspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = static_cast<uint32_t>(mips.size());
        barrier.subresourceRange.baseArrayLayer = arrayLayer;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // 6. Copy each mip level from staging buffer to image
    for (uint32_t i = 0; i < static_cast<uint32_t>(mips.size()); ++i) {
        const auto& mip = mips[i];

        VkBufferImageCopy region{};
        region.bufferOffset = layout.slices[i].offset;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0; // tightly packed
        region.imageSubresource.aspectMask = imageAspect;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = arrayLayer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mip.width, mip.height, mip.depth};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);


    }

    // 7. Barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = imageAspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = static_cast<uint32_t>(mips.size());
        barrier.subresourceRange.baseArrayLayer = arrayLayer;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cmd);

    // 8. Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (submitResult != VK_SUCCESS) {
        std::cerr << "uploadTextureData: vkQueueSubmit failed: " << submitResult << std::endl;
    }
    vkQueueWaitIdle(graphicsQueue);

    // 9. Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

RHIOffscreenTargetPtr VulkanDevice::createOffscreenTarget(
    uint32_t width, uint32_t height, Format format) {
  return VulkanOffscreenTarget::create(this, width, height, format);
}

bool VulkanDevice::readbackSwapchainImage(uint32_t imageIndex,
                                           std::vector<uint8_t>& out_rgba,
                                           uint32_t& out_width,
                                           uint32_t& out_height) {
    if (imageIndex >= swapchainImages.size()) return false;
    auto* vt = static_cast<VulkanTexture*>(swapchainImages[imageIndex].get());
    if (!vt) return false;

    const uint32_t W = swapchainExtent.width;
    const uint32_t H = swapchainExtent.height;
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(W) * H * 4;

    // Whether the swapchain is BGRA or RGBA — swizzle on CPU after readback so
    // the output is consistently RGBA. Check the raw VkFormat (the abstract
    // Format enum lossy-maps BGRA→RGBA, so we'd lose the distinction).
    const bool needSwizzleBGRA = (swapchainVkFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                                   swapchainVkFormat == VK_FORMAT_B8G8R8A8_SRGB);

    // 1. Host-visible staging buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation{};
    VmaAllocationInfo stagingAllocResult{};
    if (vmaCreateBuffer(allocator, &bufInfo, &allocCI,
                        &stagingBuffer, &stagingAllocation, &stagingAllocResult) != VK_SUCCESS) {
        return false;
    }

    // 2. One-shot command buffer for the layout transitions + copy
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbAlloc, &cmd) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                       VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = vt->getVkImage();
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // PRESENT_SRC → TRANSFER_SRC
    barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cmd, vt->getVkImage(),
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            stagingBuffer, 1, &region);

    // TRANSFER_SRC → PRESENT_SRC (restore so subsequent presents don't validate-fail)
    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }
    vkQueueWaitIdle(graphicsQueue);

    // 3. Map staging, copy to out_rgba (with BGRA→RGBA swizzle if needed)
    out_rgba.resize(byteSize);
    if (stagingAllocResult.pMappedData) {
        std::memcpy(out_rgba.data(), stagingAllocResult.pMappedData, byteSize);
    } else {
        void* p = nullptr;
        if (vmaMapMemory(allocator, stagingAllocation, &p) == VK_SUCCESS && p) {
            std::memcpy(out_rgba.data(), p, byteSize);
            vmaUnmapMemory(allocator, stagingAllocation);
        }
    }
    if (needSwizzleBGRA) {
        for (size_t i = 0; i + 3 < out_rgba.size(); i += 4) {
            std::swap(out_rgba[i], out_rgba[i + 2]);
        }
    }
    out_width = W;
    out_height = H;

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return true;
}

bool VulkanDevice::readbackTexture(const RHITexturePtr& texture,
                                   ImageLayout current_layout,
                                   std::vector<uint8_t>& out_rgba,
                                   uint32_t& out_width,
                                   uint32_t& out_height) {
    if (!texture) return false;
    auto* vt = static_cast<VulkanTexture*>(texture.get());
    if (!vt) return false;

    const uint32_t W = texture->getExtent().width;
    const uint32_t H = texture->getExtent().height;
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(W) * H * 4;
    const VkImageLayout curVk = toVkImageLayout(current_layout);

    // 1. Host-visible staging buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation{};
    VmaAllocationInfo stagingAllocResult{};
    if (vmaCreateBuffer(allocator, &bufInfo, &allocCI,
                        &stagingBuffer, &stagingAllocation, &stagingAllocResult) != VK_SUCCESS) {
        return false;
    }

    // 2. One-shot command buffer: current → TRANSFER_SRC, copy, restore.
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbAlloc, &cmd) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                       VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = vt->getVkImage();
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(curVk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cmd, vt->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, curVk,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }
    vkQueueWaitIdle(graphicsQueue);

    out_rgba.resize(byteSize);
    if (stagingAllocResult.pMappedData) {
        std::memcpy(out_rgba.data(), stagingAllocResult.pMappedData, byteSize);
    } else {
        void* p = nullptr;
        if (vmaMapMemory(allocator, stagingAllocation, &p) == VK_SUCCESS && p) {
            std::memcpy(out_rgba.data(), p, byteSize);
            vmaUnmapMemory(allocator, stagingAllocation);
        }
    }
    out_width = W;
    out_height = H;

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return true;
}

bool VulkanDevice::recordSwapchainCapture(const RHICommandBufferPtr& cmd,
                                          uint32_t imageIndex) {
    if (!cmd || imageIndex >= swapchainImages.size()) return false;
    auto* vt = static_cast<VulkanTexture*>(swapchainImages[imageIndex].get());
    if (!vt) return false;

    const uint32_t W = swapchainExtent.width;
    const uint32_t H = swapchainExtent.height;
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(W) * H * 4;

    // Persistent host-visible staging — (re)created on extent change only.
    if (captureBuffer == VK_NULL_HANDLE || captureSize < byteSize) {
        if (captureBuffer) {
            vkDeviceWaitIdle(device);
            vmaDestroyBuffer(allocator, captureBuffer, captureAllocation);
            captureBuffer = VK_NULL_HANDLE;
            captureMapped = nullptr;
        }
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = byteSize;
        bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_AUTO;
        allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                      | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(allocator, &bufInfo, &allocCI,
                            &captureBuffer, &captureAllocation, &info) != VK_SUCCESS) {
            captureBuffer = VK_NULL_HANDLE;
            return false;
        }
        captureMapped = info.pMappedData;
        captureSize   = byteSize;
    }
    captureExtent = swapchainExtent;

    VkCommandBuffer vkCmd =
        std::static_pointer_cast<VulkanCommandBuffer>(cmd)->getVkCommandBuffer();

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                       VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = vt->getVkImage();
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(vkCmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // The frame's final pass left the image in PRESENT_SRC (render pass
    // finalLayout). We are PRE-present here, so the transition is race-free —
    // that is the whole point of this API vs. readbackSwapchainImage.
    barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(vkCmd, vt->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           captureBuffer, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    return true;
}

bool VulkanDevice::readCapturedFrame(std::vector<uint8_t>& out_rgba,
                                     uint32_t& out_width,
                                     uint32_t& out_height) {
    if (!captureBuffer || !captureMapped) return false;
    const uint32_t W = captureExtent.width;
    const uint32_t H = captureExtent.height;
    const size_t byteSize = static_cast<size_t>(W) * H * 4;

    vmaInvalidateAllocation(allocator, captureAllocation, 0, VK_WHOLE_SIZE);
    out_rgba.resize(byteSize);
    std::memcpy(out_rgba.data(), captureMapped, byteSize);

    const bool needSwizzleBGRA = (swapchainVkFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                                   swapchainVkFormat == VK_FORMAT_B8G8R8A8_SRGB);
    if (needSwizzleBGRA) {
        for (size_t i = 0; i + 3 < out_rgba.size(); i += 4) {
            std::swap(out_rgba[i], out_rgba[i + 2]);
        }
    }
    out_width  = W;
    out_height = H;
    return true;
}

// ============================================================================
// Factory Implementation
// ============================================================================

RHIDevice* VulkanFactory::createDevice(
    bool enableValidation,
    const std::vector<const char*>& requiredExtensions
) {
    auto device = new VulkanDevice();
    if (!device->initializeVulkan(enableValidation, requiredExtensions, nullptr, nullptr)) {
        delete device;
        return nullptr;
    }
    return device;
}

RHIDevice* VulkanFactory::createDeviceWithSwapchain(
    void* windowHandle,
    const SwapchainDesc& swapchainDesc,
    bool enableValidation,
    const std::vector<const char*>& requiredExtensions
) {
    auto device = new VulkanDevice();
    if (!device->initializeVulkan(enableValidation, requiredExtensions, windowHandle, &swapchainDesc)) {
        delete device;
        return nullptr;
    }
    return device;
}

RHIDevice* VulkanFactory::createDeviceWithWin32Swapchain(
    void* hinstance,
    void* hwnd,
    const SwapchainDesc& swapchainDesc,
    bool enableValidation
) {
    auto device = new VulkanDevice();
    if (!device->initializeVulkanWin32(enableValidation, hinstance, hwnd, &swapchainDesc)) {
        delete device;
        return nullptr;
    }
    return device;
}

void VulkanFactory::destroyDevice(RHIDevice* device) {
    delete device;
}

} // namespace HongTu::RHI::Vulkan

// Factory function implementation
namespace HongTu::RHI::Backend {

RHIFactory* createFactory(Type type) {
    switch (type) {
        case Type::Vulkan:
            return new Vulkan::VulkanFactory();
        default:
            return nullptr;
    }
}

void destroyFactory(RHIFactory* factory) {
    delete factory;
}

} // namespace HongTu::RHI::Backend
