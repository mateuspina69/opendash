// Vulkan device + swapchain wrapped in an NVRHI device: the renderer's device outside Windows (DeviceD3D12's
// interface). Two frames in flight.
#pragma once

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace od {

class DeviceVulkan {
public:
    ~DeviceVulkan();
    bool init(SDL_Window* window, int w, int h, bool debug); // the window was created with SDL_WINDOW_VULKAN
    void resize(int w, int h); // waits idle and recreates the swapchain; 0x0 (minimised) is ignored
    nvrhi::IDevice* device() const { return device_.Get(); } // validation-wrapped in debug builds
    nvrhi::ITexture* backbuffer() const { return backbuffers_[image_].Get(); } // the acquired image
    int width() const { return width_; }
    int height() const { return height_; }
    void present(bool vsync); // presents the acquired image, waits until at most one frame is in flight, acquires the next
    bool rayQueryShaders() const { return false; } // the ray-query scene shader is not enabled on Vulkan yet

private:
    bool createSwapchain(bool vsync);
    void destroySwapchain();
    bool acquire();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice vk_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;
    bool vsync_ = true;
    std::vector<const char*> instanceExtensions_, deviceExtensions_;
    std::vector<VkSemaphore> acquireSemaphores_; // one per swapchain image, used round robin
    std::vector<VkSemaphore> presentSemaphores_; // one per swapchain image, indexed by the image
    uint32_t acquireIndex_ = 0;
    uint32_t image_ = 0;
    bool acquired_ = false;
    nvrhi::vulkan::DeviceHandle vkDevice_; // the semaphores of the swapchain go to the unwrapped device
    nvrhi::DeviceHandle device_;
    nvrhi::CommandListHandle barrierList_; // the empty submission that signals the present semaphore
    std::vector<nvrhi::EventQueryHandle> framesInFlight_;
    std::vector<nvrhi::TextureHandle> backbuffers_;
    int width_ = 1, height_ = 1;
};

} // namespace od
