// AMD FSR (FidelityFX SDK 2.3.0, ffx-api upscale provider; FSR 3.1 path on non-RDNA4 GPUs). Runs on any vendor.
#include <SDL3/SDL_log.h>
#include <windows.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.h>
#include <ffx_upscale.h>

#include "upscaler.hpp"

namespace od {

namespace {

uint32_t ffxQuality(AaQuality q) {
    switch (q) {
    case AaQuality::Quality: return FFX_UPSCALE_QUALITY_MODE_QUALITY;
    case AaQuality::Balanced: return FFX_UPSCALE_QUALITY_MODE_BALANCED;
    case AaQuality::Performance: return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
    default: return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
    }
}

FfxApiResource res(nvrhi::ITexture* t, uint32_t state) {
    return ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(t->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource)),
                                 state);
}

constexpr int kMinSize = 16;

bool loaderPresent() { return LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll") != nullptr; }

class FsrUpscaler final : public Upscaler {
public:
    ~FsrUpscaler() override {
        if (ctx_) {
            dev_->waitForIdle();
            ffxDestroyContext(&ctx_, nullptr);
        }
    }

    AaBackend backend() const override { return AaBackend::Fsr; }

    bool init(nvrhi::IDevice* dev, int outW, int outH, AaQuality q) override {
        dev_ = dev;
        if (!loaderPresent()) return false; // delay-loaded: check before the first ffx call
        if (outW < kMinSize || outH < kMinSize) return false; // ffxCreateContext aborts the process at 1x1
        ffxCreateBackendDX12Desc be{};
        be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        be.device = static_cast<ID3D12Device*>(dev->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
        ffxCreateContextDescUpscale cd{};
        cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        cd.header.pNext = &be.header;
        cd.flags = 0; // LDR colour, standard (non-inverted, finite) depth, render-resolution motion vectors
        int rw, rh;
        renderSize(outW, outH, q, rw, rh);
        cd.maxRenderSize = {uint32_t(rw), uint32_t(rh)};
        cd.maxUpscaleSize = {uint32_t(outW), uint32_t(outH)};
        if (ffxCreateContext(&ctx_, &cd.header, nullptr) != FFX_API_RETURN_OK) {
            ctx_ = nullptr;
            return false;
        }
        return true;
    }

    void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const override {
        w = outW;
        h = outH;
        if (!loaderPresent()) return;
        uint32_t rw = uint32_t(outW), rh = uint32_t(outH);
        ffxQueryDescUpscaleGetRenderResolutionFromQualityMode qd{};
        qd.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
        qd.displayWidth = uint32_t(outW);
        qd.displayHeight = uint32_t(outH);
        qd.qualityMode = ffxQuality(q);
        qd.pOutRenderWidth = &rw;
        qd.pOutRenderHeight = &rh;
        ffxQuery(nullptr, &qd.header);
        w = int(rw > 0 ? rw : 1);
        h = int(rh > 0 ? rh : 1);
    }

    bool temporal() const override { return true; }

    uint32_t phaseCount(int renderW, int outW) const override {
        int32_t n = 8;
        ffxQueryDescUpscaleGetJitterPhaseCount qd{};
        qd.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        qd.renderWidth = uint32_t(renderW);
        qd.displayWidth = uint32_t(outW);
        qd.pOutPhaseCount = &n;
        ffxQuery(ctx_ ? const_cast<ffxContext*>(&ctx_) : nullptr, &qd.header);
        return uint32_t(n > 0 ? n : 8);
    }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        for (nvrhi::ITexture* t : {in.color, in.depth, in.motion, in.reactive})
            cl->setTextureState(t, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        cl->setTextureState(in.output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        cl->commitBarriers();

        ffxDispatchDescUpscale d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = cl->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        d.color = res(in.color, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.depth = res(in.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.motionVectors = res(in.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.reactive = res(in.reactive, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.output = res(in.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
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
        if (ffxDispatch(&ctx_, &d.header) != FFX_API_RETURN_OK) {
            SDL_Log("fsr: dispatch failed");
            fail(cl, in);
        }
    }

    bool failed() const override { return failed_; }

private:
    void fail(nvrhi::ICommandList* cl, const UpscalerInputs& in) { // keep the frame, let the renderer switch
        failed_ = true;
        cl->copyTexture(in.output, nvrhi::TextureSlice(), in.color, nvrhi::TextureSlice());
    }
    bool failed_ = false;
    nvrhi::IDevice* dev_ = nullptr;
    ffxContext ctx_ = nullptr;
};

} // namespace

std::unique_ptr<Upscaler> createFsrUpscaler() { return std::make_unique<FsrUpscaler>(); }

} // namespace od
