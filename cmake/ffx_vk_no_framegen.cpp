// The FidelityFX Vulkan backend's interface names the frame-generation swapchain's config entry point; OpenDash builds
// the upscaler only (cmake/fidelityfx_vk.cmake), without FrameInterpolationSwapchain/: frame generation is unsupported.
#include <FidelityFX/host/backends/vk/ffx_vk.h>

FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
    return FFX_ERROR_BACKEND_API_ERROR;
}
