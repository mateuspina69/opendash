// OpenDash's own anti-aliasing, no SDK, any GPU: SSAA (the scene at 2x2 the output, averaged down) and MSAA (4x
// multisampled scene targets, resolved by the renderer before the upscale; the colour is then copied as is).
#include "upscaler.hpp"

#include "g_ssaa_cs.h"

namespace od {

namespace {

constexpr int kSsaaScale = 2;
constexpr int kMsaaSamples = 4;

class SsaaUpscaler final : public Upscaler {
public:
    AaBackend backend() const override { return AaBackend::Ssaa; }

    bool init(nvrhi::IDevice* dev, int, int, AaQuality) override {
        dev_ = dev;
        cs_ = dev->createShader({nvrhi::ShaderType::Compute, "", "cs_main"}, g_ssaa_cs, sizeof(g_ssaa_cs));
        nvrhi::BindingLayoutDesc bl;
        bl.visibility = nvrhi::ShaderType::Compute;
        bl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(Params)), nvrhi::BindingLayoutItem::Sampler(0),
                       nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_UAV(0)};
        layout_ = dev->createBindingLayout(bl);
        nvrhi::ComputePipelineDesc pd;
        pd.CS = cs_;
        pd.bindingLayouts = {layout_};
        pipeline_ = dev->createComputePipeline(pd);
        sampler_ = dev->createSampler(
            nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        return pipeline_ != nullptr;
    }

    void renderSize(int outW, int outH, AaQuality, int& w, int& h) const override { // the quality has no say
        w = outW * kSsaaScale;
        h = outH * kSsaaScale;
    }

    bool temporal() const override { return false; }
    uint32_t phaseCount(int, int) const override { return 1; }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        if (in.color != setColor_ || in.output != setOutput_) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(Params)),
                           nvrhi::BindingSetItem::Sampler(0, sampler_), nvrhi::BindingSetItem::Texture_SRV(0, in.color),
                           nvrhi::BindingSetItem::Texture_UAV(0, in.output)};
            set_ = dev_->createBindingSet(bs, layout_);
            setColor_ = in.color;
            setOutput_ = in.output;
        }
        nvrhi::ComputeState cs;
        cs.pipeline = pipeline_;
        cs.bindings = {set_};
        cl->setComputeState(cs);
        const Params p{{uint32_t(in.outW), uint32_t(in.outH)}, {}};
        cl->setPushConstants(&p, sizeof p);
        cl->dispatch((uint32_t(in.outW) + 7) / 8, (uint32_t(in.outH) + 7) / 8);
    }

private:
    struct Params { // ssaa.hlsl
        uint32_t outSize[2];
        uint32_t pad[2];
    };
    nvrhi::IDevice* dev_ = nullptr;
    nvrhi::ShaderHandle cs_;
    nvrhi::BindingLayoutHandle layout_;
    nvrhi::ComputePipelineHandle pipeline_;
    nvrhi::SamplerHandle sampler_;
    nvrhi::BindingSetHandle set_;
    nvrhi::ITexture* setColor_ = nullptr;
    nvrhi::ITexture* setOutput_ = nullptr;
};

class MsaaUpscaler final : public Upscaler {
public:
    AaBackend backend() const override { return AaBackend::Msaa; }
    bool init(nvrhi::IDevice*, int, int, AaQuality) override { return true; }
    void renderSize(int outW, int outH, AaQuality, int& w, int& h) const override {
        w = outW;
        h = outH;
    }
    bool temporal() const override { return false; }
    uint32_t phaseCount(int, int) const override { return 1; }
    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        cl->copyTexture(in.output, nvrhi::TextureSlice(), in.color, nvrhi::TextureSlice());
    }
    int sampleCount() const override { return kMsaaSamples; }
};

} // namespace

std::unique_ptr<Upscaler> createSsaaUpscaler() { return std::make_unique<SsaaUpscaler>(); }
std::unique_ptr<Upscaler> createMsaaUpscaler() { return std::make_unique<MsaaUpscaler>(); }

} // namespace od
