// AMD FSR 3.1 on Vulkan (FidelityFX SDK 1.1.4's upscaler component and Vulkan backend, built from source:
// cmake/fidelityfx_vk.cmake). The renderer outside Windows; upscaler_fsr.cpp is the D3D12 one. Runs on any vendor.
#include <SDL3/SDL_log.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cstring>
#include <vector>

#include "upscaler.hpp"

namespace od {

namespace {

FfxFsr3UpscalerQualityMode ffxQuality(AaQuality q) {
    switch (q) {
    case AaQuality::Quality: return FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY;
    case AaQuality::Balanced: return FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED;
    case AaQuality::Performance: return FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE;
    default: return FFX_FSR3UPSCALER_QUALITY_MODE_NATIVEAA;
    }
}

FfxResource res(nvrhi::ITexture* t, FfxResourceStates state, const wchar_t* name) {
    const nvrhi::TextureDesc& td = t->getDesc();
    FfxResourceDescription d{};
    d.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    d.format = ffxGetSurfaceFormatVK(nvrhi::vulkan::convertFormat(td.format));
    d.width = td.width;
    d.height = td.height;
    d.depth = 1;
    d.mipCount = 1;
    d.flags = FFX_RESOURCE_FLAGS_NONE;
    d.usage = state == FFX_RESOURCE_STATE_UNORDERED_ACCESS ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY;
    return ffxGetResourceVK(t->getNativeObject(nvrhi::ObjectTypes::VK_Image), d, name, state);
}

constexpr int kMinSize = 16;

// the backend asks for the KHR names of functions core since Vulkan 1.1, whose extensions our device does not enable
PFN_vkVoidFunction deviceProcAddr(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkGetBufferMemoryRequirements2KHR") == 0) name = "vkGetBufferMemoryRequirements2";
    return vkGetDeviceProcAddr(device, name);
}

class FsrVkUpscaler final : public Upscaler {
public:
    ~FsrVkUpscaler() override {
        if (live_) {
            dev_->waitForIdle();
            for (FfxResourceInternal& r : shared_) iface_.fpDestroyResource(&iface_, r, 0);
            ffxFsr3UpscalerContextDestroy(&ctx_);
        }
    }

    AaBackend backend() const override { return AaBackend::Fsr; }

    bool init(nvrhi::IDevice* dev, int outW, int outH, AaQuality q) override {
        dev_ = dev;
        if (outW < kMinSize || outH < kMinSize) return false;
        auto* physical = static_cast<VkPhysicalDevice>(dev->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice));
        VkDeviceContext vk{};
        vk.vkDevice = static_cast<VkDevice>(dev->getNativeObject(nvrhi::ObjectTypes::VK_Device));
        vk.vkPhysicalDevice = physical;
        vk.vkDeviceProcAddr = deviceProcAddr;
        const size_t bytes = ffxGetScratchMemorySizeVK(physical, FFX_FSR3UPSCALER_CONTEXT_COUNT);
        scratch_.resize((bytes + sizeof(ScratchLine) - 1) / sizeof(ScratchLine));
        FfxFsr3UpscalerContextDescription cd{};
        if (ffxGetInterfaceVK(&cd.backendInterface, ffxGetDeviceVK(&vk), scratch_.data(),
                              scratch_.size() * sizeof(ScratchLine), FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK)
            return false;
        cd.flags = 0; // LDR colour, standard (non-inverted, finite) depth, render-resolution motion vectors
        int rw, rh;
        renderSize(outW, outH, q, rw, rh);
        cd.maxRenderSize = {uint32_t(rw), uint32_t(rh)};
        cd.maxUpscaleSize = {uint32_t(outW), uint32_t(outH)};
        if (ffxFsr3UpscalerContextCreate(&ctx_, &cd) != FFX_OK) return false;
        live_ = true;
        iface_ = cd.backendInterface;
        // the surfaces FSR 3.1 shares with frame generation: the caller makes them (ffx-api's upscale provider does)
        FfxFsr3UpscalerSharedResourceDescriptions sd{};
        if (ffxFsr3UpscalerGetSharedResourceDescriptions(&ctx_, &sd) != FFX_OK) return false;
        FfxCreateResourceDescription* descs[] = {&sd.dilatedDepth, &sd.dilatedMotionVectors,
                                                 &sd.reconstructedPrevNearestDepth};
        for (int i = 0; i < 3; ++i)
            if (iface_.fpCreateResource(&iface_, descs[i], 0, &shared_[i]) != FFX_OK) return false;
        return true;
    }

    void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const override {
        uint32_t rw = uint32_t(outW), rh = uint32_t(outH);
        ffxFsr3UpscalerGetRenderResolutionFromQualityMode(&rw, &rh, uint32_t(outW), uint32_t(outH), ffxQuality(q));
        w = int(rw > 0 ? rw : 1);
        h = int(rh > 0 ? rh : 1);
    }

    bool temporal() const override { return true; }

    uint32_t phaseCount(int renderW, int outW) const override {
        const int32_t n = ffxFsr3UpscalerGetJitterPhaseCount(renderW, outW);
        return uint32_t(n > 0 ? n : 8);
    }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        for (nvrhi::ITexture* t : {in.color, in.depth, in.motion, in.reactive})
            cl->setTextureState(t, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        cl->setTextureState(in.output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        cl->commitBarriers();

        FfxFsr3UpscalerDispatchDescription d{};
        d.commandList = ffxGetCommandListVK(
            static_cast<VkCommandBuffer>(cl->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)));
        d.color = res(in.color, FFX_RESOURCE_STATE_COMPUTE_READ, L"FSR3_InputColor");
        d.depth = res(in.depth, FFX_RESOURCE_STATE_COMPUTE_READ, L"FSR3_InputDepth");
        d.motionVectors = res(in.motion, FFX_RESOURCE_STATE_COMPUTE_READ, L"FSR3_InputMotionVectors");
        d.reactive = res(in.reactive, FFX_RESOURCE_STATE_COMPUTE_READ, L"FSR3_InputReactiveMap");
        d.output = res(in.output, FFX_RESOURCE_STATE_UNORDERED_ACCESS, L"FSR3_UpscaledOutput");
        d.dilatedDepth = iface_.fpGetResource(&iface_, shared_[0]);
        d.dilatedMotionVectors = iface_.fpGetResource(&iface_, shared_[1]);
        d.reconstructedPrevNearestDepth = iface_.fpGetResource(&iface_, shared_[2]);
        d.jitterOffset = {in.jitterX, in.jitterY}; // the pixel offset applied to the projection
        d.motionVectorScale = {1.0f, 1.0f};        // our motion vectors are pixels, prev - cur
        d.renderSize = {uint32_t(in.renderW), uint32_t(in.renderH)};
        d.upscaleSize = {uint32_t(in.outW), uint32_t(in.outH)};
        d.frameTimeDelta = in.frameTimeMs;
        d.preExposure = 1.0f;
        d.reset = in.reset;
        d.cameraNear = in.nearZ;
        d.cameraFar = in.farZ;
        d.cameraFovAngleVertical = in.fovY;
        d.viewSpaceToMetersFactor = 1.0f / 125.0f; // Megaman (~200 units) ~ 1.6 m
        if (ffxFsr3UpscalerContextDispatch(&ctx_, &d) != FFX_OK) {
            SDL_Log("fsr: dispatch failed");
            fail(cl, in);
        }
        // the dispatch recorded its own barriers and left our textures in the states it was given; NVRHI's
        // bindings are stale after native work
        cl->clearState();
    }

    bool failed() const override { return failed_; }

private:
    void fail(nvrhi::ICommandList* cl, const UpscalerInputs& in) { // keep the frame, let the renderer switch
        failed_ = true;
        cl->copyTexture(in.output, nvrhi::TextureSlice(), in.color, nvrhi::TextureSlice());
    }
    bool failed_ = false;
    bool live_ = false;
    nvrhi::IDevice* dev_ = nullptr;
    // the backend's state; 64-aligned, as tools/fetch_sdks.py's patch lays the backend's arrays out in it
    struct alignas(64) ScratchLine {
        uint8_t bytes[64];
    };
    std::vector<ScratchLine> scratch_;
    FfxFsr3UpscalerContext ctx_{};
    FfxInterface iface_{};
    FfxResourceInternal shared_[3]{}; // dilated depth, dilated motion vectors, reconstructed previous nearest depth
};

} // namespace

std::unique_ptr<Upscaler> createFsrUpscaler() { return std::make_unique<FsrVkUpscaler>(); }

} // namespace od
