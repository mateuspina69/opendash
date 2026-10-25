// NVIDIA DLSS / DLAA through NGX directly (DLSS SDK 310.9.1). RTX GPUs only; init fails elsewhere.
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>

#include <string>

#include "render_logic.hpp"
#include "upscaler.hpp"

namespace od {

namespace {

constexpr const char* kProjectId = "6d1b8a0e-3c52-4f7a-9e1d-0b5a7c2f4e93"; // OpenDash (custom engine)

// NGX is initialised once per device; the probe in availableBackends() and the active backend share it
int g_ngxUsers = 0;
NVSDK_NGX_Parameter* g_caps = nullptr;

bool ngxAcquire(ID3D12Device* dev) {
    if (g_ngxUsers > 0) {
        ++g_ngxUsers;
        return true;
    }
    const char* base = SDL_GetBasePath();
    const std::string dir = base ? base : ".";
    const std::wstring wdir(dir.begin(), dir.end()); // NGX logs / caches next to the exe
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.1",
                                                              wdir.c_str(), dev)))
        return false;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_GetCapabilityParameters(&g_caps))) {
        NVSDK_NGX_D3D12_Shutdown1(dev);
        return false;
    }
    g_ngxUsers = 1;
    return true;
}

// NGX stays initialised once a DLSS upscaler was created (one NVSDK_NGX_D3D12_Init per process: each init makes the
// driver start nvngx_update.exe; recreating the upscaler on every resize / AA change used to init it again)
void ngxRelease(ID3D12Device*) {
    if (g_ngxUsers > 1) --g_ngxUsers;
}

NVSDK_NGX_PerfQuality_Value perfQuality(AaQuality q) {
    switch (q) {
    case AaQuality::Quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case AaQuality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case AaQuality::Performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    default: return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
}

ID3D12Resource* native(nvrhi::ITexture* t) {
    return static_cast<ID3D12Resource*>(t->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource));
}

class DlssUpscaler final : public Upscaler {
public:
    ~DlssUpscaler() override {
        if (!d3d_) return;
        dev_->waitForIdle();
        if (feature_) NVSDK_NGX_D3D12_ReleaseFeature(feature_);
        ngxRelease(d3d_);
    }

    AaBackend backend() const override { return AaBackend::Dlss; }

    bool init(nvrhi::IDevice* dev, int outW, int outH, AaQuality q) override {
        dev_ = dev;
        ID3D12Device* d3d = static_cast<ID3D12Device*>(dev->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
        if (!ngxAcquire(d3d)) return false;
        d3d_ = d3d;
        int available = 0;
        if (NVSDK_NGX_FAILED(NVSDK_NGX_Parameter_GetI(g_caps, NVSDK_NGX_Parameter_SuperSampling_Available,
                                                       &available)) ||
            !available)
            return false; // destructor releases NGX
        quality_ = q;
        renderSize(outW, outH, q, renderW_, renderH_);
        outW_ = outW;
        outH_ = outH;
        return true;
    }

    void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const override {
        w = outW;
        h = outH;
        if (q == AaQuality::NativeAA || !g_caps) return; // DLAA: render = output
        unsigned rw = 0, rh = 0, maxW, maxH, minW, minH;
        float sharpness = 0.0f;
        if (NVSDK_NGX_SUCCEED(NGX_DLSS_GET_OPTIMAL_SETTINGS(g_caps, unsigned(outW), unsigned(outH), perfQuality(q), &rw,
                                                            &rh, &maxW, &maxH, &minW, &minH, &sharpness)) &&
            rw && rh) {
            w = int(rw);
            h = int(rh);
        }
    }

    bool temporal() const override { return true; }
    uint32_t phaseCount(int renderW, int outW) const override { return dlssPhaseCount(renderW, outW); }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        auto* cmd = static_cast<ID3D12GraphicsCommandList*>(
            cl->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList));
        for (nvrhi::ITexture* t : {in.color, in.depth, in.motion, in.reactive})
            cl->setTextureState(t, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        cl->setTextureState(in.output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        cl->commitBarriers();

        if (!feature_) { // created lazily: NGX needs a command list
            NVSDK_NGX_DLSS_Create_Params cp{};
            cp.Feature.InWidth = unsigned(in.renderW);
            cp.Feature.InHeight = unsigned(in.renderH);
            cp.Feature.InTargetWidth = unsigned(in.outW);
            cp.Feature.InTargetHeight = unsigned(in.outH);
            cp.Feature.InPerfQualityValue = perfQuality(quality_);
            cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes; // LDR, standard depth, render-res MVs
            if (NVSDK_NGX_FAILED(NGX_D3D12_CREATE_DLSS_EXT(cmd, 1, 1, &feature_, g_caps, &cp))) {
                SDL_Log("dlss: feature creation failed");
                feature_ = nullptr;
                fail(cl, in);
                return;
            }
        }
        NVSDK_NGX_D3D12_DLSS_Eval_Params ep{};
        ep.Feature.pInColor = native(in.color);
        ep.Feature.pInOutput = native(in.output);
        ep.pInDepth = native(in.depth);
        ep.pInMotionVectors = native(in.motion);
        ep.pInBiasCurrentColorMask = native(in.reactive); // semi-transparent surfaces trust the current frame more
        ep.InJitterOffsetX = in.jitterX;
        ep.InJitterOffsetY = in.jitterY;
        ep.InRenderSubrectDimensions = {unsigned(in.renderW), unsigned(in.renderH)};
        ep.InReset = in.reset ? 1 : 0;
        ep.InMVScaleX = 1.0f; // pixels, prev - cur
        ep.InMVScaleY = 1.0f;
        ep.InFrameTimeDeltaInMsec = in.frameTimeMs;
        if (NVSDK_NGX_FAILED(NGX_D3D12_EVALUATE_DLSS_EXT(cmd, feature_, g_caps, &ep))) {
            SDL_Log("dlss: evaluate failed");
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
    ID3D12Device* d3d_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    AaQuality quality_ = AaQuality::NativeAA;
    int renderW_ = 0, renderH_ = 0, outW_ = 0, outH_ = 0;
};

} // namespace

std::unique_ptr<Upscaler> createDlssUpscaler() { return std::make_unique<DlssUpscaler>(); }

} // namespace od
