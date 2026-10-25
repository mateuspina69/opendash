// Intel XeSS (SDK 3.0.2). XMX path on Arc, DP4a path on other SM 6.4 GPUs.
#include <SDL3/SDL_log.h>

#include <windows.h>

#include <d3d12.h>
#include <xess/xess_d3d12.h>

#include "render_logic.hpp"
#include "upscaler.hpp"

namespace od {

namespace {

xess_quality_settings_t xessQuality(AaQuality q) {
    switch (q) {
    case AaQuality::Quality: return XESS_QUALITY_SETTING_QUALITY;
    case AaQuality::Balanced: return XESS_QUALITY_SETTING_BALANCED;
    case AaQuality::Performance: return XESS_QUALITY_SETTING_PERFORMANCE;
    default: return XESS_QUALITY_SETTING_AA;
    }
}

ID3D12Resource* native(nvrhi::ITexture* t) {
    return static_cast<ID3D12Resource*>(t->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource));
}

class XessUpscaler final : public Upscaler {
public:
    ~XessUpscaler() override {
        if (!ctx_) return;
        dev_->waitForIdle();
        xessDestroyContext(ctx_);
    }

    AaBackend backend() const override { return AaBackend::Xess; }

    bool init(nvrhi::IDevice* dev, int outW, int outH, AaQuality q) override {
        dev_ = dev;
        if (!LoadLibraryW(L"libxess.dll")) return false; // delay-loaded: check before the first xess call
        auto* d3d = static_cast<ID3D12Device*>(dev->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
        if (xessD3D12CreateContext(d3d, &ctx_) != XESS_RESULT_SUCCESS) {
            ctx_ = nullptr;
            return false;
        }
        xess_d3d12_init_params_t p{};
        p.outputResolution = {uint32_t(outW), uint32_t(outH)};
        p.qualitySetting = xessQuality(q);
        // LDR colour, standard depth, render-resolution pixel motion vectors, reactive mask as responsive mask
        p.initFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
        if (xessD3D12Init(ctx_, &p) != XESS_RESULT_SUCCESS) {
            xessDestroyContext(ctx_);
            ctx_ = nullptr;
            return false;
        }
        xessSetVelocityScale(ctx_, 1.0f, 1.0f); // pixels, prev - cur
        return true;
    }

    void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const override {
        w = outW;
        h = outH;
        if (!ctx_) return;
        const xess_2d_t out{uint32_t(outW), uint32_t(outH)};
        xess_2d_t in{};
        if (xessGetInputResolution(ctx_, &out, xessQuality(q), &in) == XESS_RESULT_SUCCESS && in.x && in.y) {
            w = int(in.x);
            h = int(in.y);
        }
    }

    bool temporal() const override { return true; }
    uint32_t phaseCount(int renderW, int outW) const override { return dlssPhaseCount(renderW, outW); }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        for (nvrhi::ITexture* t : {in.color, in.depth, in.motion, in.reactive})
            cl->setTextureState(t, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        cl->setTextureState(in.output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        cl->commitBarriers();
        xess_d3d12_execute_params_t ep{};
        ep.pColorTexture = native(in.color);
        ep.pVelocityTexture = native(in.motion);
        ep.pDepthTexture = native(in.depth);
        ep.pResponsivePixelMaskTexture = native(in.reactive);
        ep.pOutputTexture = native(in.output);
        ep.jitterOffsetX = in.jitterX;
        ep.jitterOffsetY = in.jitterY;
        ep.exposureScale = 1.0f;
        ep.resetHistory = in.reset ? 1u : 0u;
        ep.inputWidth = uint32_t(in.renderW);
        ep.inputHeight = uint32_t(in.renderH);
        auto* cmd = static_cast<ID3D12GraphicsCommandList*>(
            cl->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList));
        if (xessD3D12Execute(ctx_, cmd, &ep) != XESS_RESULT_SUCCESS) {
            SDL_Log("xess: execute failed");
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
    xess_context_handle_t ctx_ = nullptr;
};

} // namespace

std::unique_ptr<Upscaler> createXessUpscaler() { return std::make_unique<XessUpscaler>(); }

} // namespace od
