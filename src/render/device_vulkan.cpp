#include "device_vulkan.hpp"

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>

// NVRHI's Vulkan backend calls through vulkan.hpp's dynamic dispatcher; a static NVRHI leaves its storage (exactly one
// in the program) and its initialization to the app
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace od {

namespace {

constexpr size_t kFramesInFlight = 2;

struct LogCallback final : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        static const char* kNames[] = {"info", "warning", "error", "fatal"};
        if (severity == nvrhi::MessageSeverity::Info) return;
        SDL_Log("nvrhi %s: %s", kNames[int(severity)], text);
    }
};
LogCallback g_log;

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) SDL_Log("vulkan: %s", data->pMessage);
    return VK_FALSE;
}

bool hasLayer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) { return !strcmp(l.layerName, name); });
}

bool hasDeviceExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) { return !strcmp(e.extensionName, name); });
}

} // namespace

DeviceVulkan::~DeviceVulkan() {
    if (device_) device_->waitForIdle();
    framesInFlight_.clear();
    barrierList_ = nullptr;
    destroySwapchain();
    device_ = nullptr;
    vkDevice_ = nullptr;
    if (vk_) vkDestroyDevice(vk_, nullptr);
    if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    if (messenger_) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, messenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

bool DeviceVulkan::init(SDL_Window* window, int w, int h, bool debug) {
    width_ = w > 0 ? w : 1;
    height_ = h > 0 ? h : 1;

    // the instance: the window system's surface extensions (SDL), the debug messenger and validation in debug builds
    Uint32 sdlCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    if (!sdlExts) {
        SDL_Log("vulkan: no loader / surface extensions (%s)", SDL_GetError());
        return false;
    }
    instanceExtensions_.assign(sdlExts, sdlExts + sdlCount);
    std::vector<const char*> layers;
    // the validation layer is optional (the vulkan-validation-layers package): without it, run without
    if (debug && hasLayer("VK_LAYER_KHRONOS_validation")) layers.push_back("VK_LAYER_KHRONOS_validation");
    else if (debug) SDL_Log("vulkan: validation layer unavailable (vulkan-validation-layers not installed?), continuing without it");
    if (debug) instanceExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "OpenDash";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(instanceExtensions_.size());
    ici.ppEnabledExtensionNames = instanceExtensions_.data();
    ici.enabledLayerCount = uint32_t(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
        SDL_Log("vulkan: vkCreateInstance failed (no Vulkan 1.3 driver?)");
        return false;
    }
    if (debug) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = debugMessage;
        if (create) create(instance_, &mci, nullptr, &messenger_);
    }
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        SDL_Log("vulkan: SDL_Vulkan_CreateSurface failed (%s)", SDL_GetError());
        return false;
    }

    // the physical device: Vulkan 1.3 with what NVRHI's backend uses unconditionally (timeline semaphores, dynamic
    // rendering, synchronization2), a graphics queue that presents to the surface, the swapchain; a discrete GPU first
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    vkEnumeratePhysicalDevices(instance_, &n, devices.data());
    int bestScore = -1;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.apiVersion < VK_API_VERSION_1_3 || !hasDeviceExtension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        if (!f12.timelineSemaphore || !f13.dynamicRendering || !f13.synchronization2) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> families(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, families.data());
        for (uint32_t q = 0; q < qn; ++q) {
            VkBool32 presents = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, q, surface_, &presents);
            if (!(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !presents) continue;
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2
                              : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
            if (score > bestScore) {
                bestScore = score;
                physical_ = pd;
                queueFamily_ = q;
            }
            break;
        }
    }
    if (!physical_) {
        SDL_Log("vulkan: no Vulkan 1.3 GPU (dynamic rendering, synchronization2) that presents to this window");
        return false;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_, &props);
    SDL_Log("vulkan: %s (Vulkan %u.%u)", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion));

    deviceExtensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // D3D12's DepthClipEnable = FALSE (NVRHI's default raster state, the renderer's): nothing is clipped at the near
    // plane -- a face leaning into the camera stays whole (the opening cutscene's Data). Vulkan clips unless
    // VK_EXT_depth_clip_enable turns it off; NVRHI uses it when it is enabled
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
    if (hasDeviceExtension(physical_, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &depthClip};
        vkGetPhysicalDeviceFeatures2(physical_, &query);
    }
    if (depthClip.depthClipEnable) deviceExtensions_.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
    else SDL_Log("vulkan: no VK_EXT_depth_clip_enable: geometry is clipped at the near plane (unlike D3D12)");
    depthClip.pNext = nullptr;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.pNext = depthClip.depthClipEnable ? &depthClip : nullptr;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
    f12.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(physical_, &supported);
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
    f2.features.samplerAnisotropy = supported.samplerAnisotropy;
    f2.features.independentBlend = supported.independentBlend; // the scene's four targets blend differently
    f2.features.fillModeNonSolid = supported.fillModeNonSolid;
    f2.features.depthClamp = supported.depthClamp;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(deviceExtensions_.size());
    dci.ppEnabledExtensionNames = deviceExtensions_.data();
    if (vkCreateDevice(physical_, &dci, nullptr, &vk_) != VK_SUCCESS) {
        SDL_Log("vulkan: vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(vk_, queueFamily_, 0, &queue_);

    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_, vkGetInstanceProcAddr, vk_);
    nvrhi::vulkan::DeviceDesc dd;
    dd.errorCB = &g_log;
    dd.instance = instance_;
    dd.physicalDevice = physical_;
    dd.device = vk_;
    dd.graphicsQueue = queue_;
    dd.graphicsQueueIndex = int(queueFamily_);
    dd.instanceExtensions = instanceExtensions_.data();
    dd.numInstanceExtensions = instanceExtensions_.size();
    dd.deviceExtensions = deviceExtensions_.data();
    dd.numDeviceExtensions = deviceExtensions_.size();
    vkDevice_ = nvrhi::vulkan::createDevice(dd);
    if (!vkDevice_) return false;
    device_ = vkDevice_;
    if (debug) device_ = nvrhi::validation::createValidationLayer(device_);
    barrierList_ = device_->createCommandList();
    return createSwapchain(true) && acquire();
}

bool DeviceVulkan::createSwapchain(bool vsync) {
    vsync_ = vsync;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    // the renderer draws to an RGBA8 UNORM backbuffer; BGRA8 is the one every Linux driver offers
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (VkFormat want : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
        auto it = std::find_if(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) { return f.format == want; });
        if (it != formats.end()) {
            chosen = *it;
            break;
        }
    }
    format_ = chosen.format;
    // vsync: FIFO (always there); off: MAILBOX, else IMMEDIATE
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, modes.data());
        for (VkPresentModeKHR want : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
            if (std::find(modes.begin(), modes.end(), want) != modes.end()) {
                mode = want;
                break;
            }
    }
    // the surface's size wins when it has one (Wayland leaves it to the swapchain: 0xFFFFFFFF)
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        width_ = int(caps.currentExtent.width);
        height_ = int(caps.currentExtent.height);
    }
    width_ = std::clamp(width_, int(caps.minImageExtent.width), int(caps.maxImageExtent.width));
    height_ = std::clamp(height_, int(caps.minImageExtent.height), int(caps.maxImageExtent.height));
    uint32_t images = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount) images = std::min(images, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = images;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = {uint32_t(width_), uint32_t(height_)};
    sci.imageArrayLayers = 1;
    // the copies out of the back buffer: the scene grab, the screenshots
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(vk_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
        SDL_Log("vulkan: swapchain creation failed");
        return false;
    }
    vkGetSwapchainImagesKHR(vk_, swapchain_, &n, nullptr);
    std::vector<VkImage> vkImages(n);
    vkGetSwapchainImagesKHR(vk_, swapchain_, &n, vkImages.data());
    const nvrhi::Format format = format_ == VK_FORMAT_B8G8R8A8_UNORM ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::RGBA8_UNORM;
    for (VkImage image : vkImages) {
        nvrhi::TextureDesc td;
        td.width = uint32_t(width_);
        td.height = uint32_t(height_);
        td.format = format;
        td.debugName = "backbuffer";
        td.isRenderTarget = true;
        td.initialState = nvrhi::ResourceStates::Present;
        td.keepInitialState = true;
        backbuffers_.push_back(device_->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), td));
        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore a, p;
        vkCreateSemaphore(vk_, &semi, nullptr, &a);
        vkCreateSemaphore(vk_, &semi, nullptr, &p);
        acquireSemaphores_.push_back(a);
        presentSemaphores_.push_back(p);
    }
    acquireIndex_ = 0;
    return true;
}

void DeviceVulkan::destroySwapchain() {
    if (vk_) vkDeviceWaitIdle(vk_);
    backbuffers_.clear();
    if (device_) device_->runGarbageCollection();
    for (VkSemaphore s : acquireSemaphores_) vkDestroySemaphore(vk_, s, nullptr);
    for (VkSemaphore s : presentSemaphores_) vkDestroySemaphore(vk_, s, nullptr);
    acquireSemaphores_.clear();
    presentSemaphores_.clear();
    if (swapchain_) vkDestroySwapchainKHR(vk_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    acquired_ = false;
}

bool DeviceVulkan::acquire() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        VkSemaphore s = acquireSemaphores_[acquireIndex_];
        const VkResult r = vkAcquireNextImageKHR(vk_, swapchain_, UINT64_MAX, s, VK_NULL_HANDLE, &image_);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            acquireIndex_ = (acquireIndex_ + 1) % uint32_t(acquireSemaphores_.size());
            // the next submission (the renderer's frame) waits until the image is free
            vkDevice_->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, s, 0);
            acquired_ = true;
            return true;
        }
        if (r != VK_ERROR_OUT_OF_DATE_KHR) break;
        destroySwapchain(); // the window changed under the swapchain: recreate at the surface's size
        if (!createSwapchain(vsync_)) return false;
    }
    SDL_Log("vulkan: vkAcquireNextImageKHR failed");
    return false;
}

void DeviceVulkan::resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == width_ && h == height_)) return;
    device_->waitForIdle();
    destroySwapchain();
    width_ = w;
    height_ = h;
    if (createSwapchain(vsync_)) acquire();
}

void DeviceVulkan::present(bool vsync) {
    if (!acquired_) return;
    // an empty submission after the frame's: it signals the present semaphore once the frame is done
    VkSemaphore done = presentSemaphores_[image_];
    vkDevice_->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, done, 0);
    barrierList_->open();
    barrierList_->close();
    device_->executeCommandList(barrierList_);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_;
    const VkResult r = vkQueuePresentKHR(queue_, &pi);
    acquired_ = false;

    // two frames in flight: wait for the frame before this one
    nvrhi::EventQueryHandle q;
    if (framesInFlight_.size() >= kFramesInFlight) {
        q = framesInFlight_.front();
        framesInFlight_.erase(framesInFlight_.begin());
        device_->waitEventQuery(q);
        device_->resetEventQuery(q);
    } else {
        q = device_->createEventQuery();
    }
    device_->setEventQuery(q, nvrhi::CommandQueue::Graphics);
    framesInFlight_.push_back(q);
    device_->runGarbageCollection();

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || vsync != vsync_) {
        destroySwapchain();
        if (!createSwapchain(vsync)) return;
    }
    acquire();
}

} // namespace od
