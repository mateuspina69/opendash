// NVIDIA Image Scaling 1.0.3: spatial upscale + sharpen (no history, no jitter). Any GPU; native = sharpen only.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <NIS_Config.h>

#include "g_nis_scaler_cs.h"
#include "g_nis_sharpen_cs.h"
#include "render_logic.hpp"
#include "upscaler.hpp"

namespace od {

namespace {

constexpr uint32_t kBlockW = 32, kScalerBlockH = 24, kSharpenBlockH = 32; // NISOptimizer generic values
constexpr float kSharpness = 0.25f;

class NisUpscaler final : public Upscaler {
public:
    AaBackend backend() const override { return AaBackend::Nis; }

    bool init(nvrhi::IDevice* dev, int, int, AaQuality q) override {
        dev_ = dev;
        scaling_ = q != AaQuality::NativeAA;
        cs_ = scaling_ ? dev->createShader({nvrhi::ShaderType::Compute, "", "main"}, g_nis_scaler_cs,
                                           sizeof(g_nis_scaler_cs))
                       : dev->createShader({nvrhi::ShaderType::Compute, "", "main"}, g_nis_sharpen_cs,
                                           sizeof(g_nis_sharpen_cs));
        nvrhi::BufferDesc cb;
        cb.byteSize = sizeof(NISConfig);
        cb.isConstantBuffer = true;
        cb.isVolatile = true;
        cb.maxVersions = 16;
        cb.debugName = "nis config";
        cb_ = dev->createBuffer(cb);
        sampler_ = dev->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(
            nvrhi::SamplerAddressMode::Clamp));

        nvrhi::BindingLayoutDesc bl;
        bl.visibility = nvrhi::ShaderType::Compute;
        bl.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Sampler(0),
                       nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_UAV(0)};
        if (scaling_) {
            bl.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
        }
        layout_ = dev->createBindingLayout(bl);
        nvrhi::ComputePipelineDesc pd;
        pd.CS = cs_;
        pd.bindingLayouts = {layout_};
        pipeline_ = dev->createComputePipeline(pd);

        if (scaling_) { // filter coefficients: kPhaseCount rows of kFilterSize floats (RGBA32F, 2 texels per row)
            nvrhi::TextureDesc td;
            td.width = uint32_t(kFilterSize / 4);
            td.height = uint32_t(kPhaseCount);
            td.format = nvrhi::Format::RGBA32_FLOAT;
            td.initialState = nvrhi::ResourceStates::ShaderResource;
            td.keepInitialState = true;
            td.debugName = "nis coef scale";
            coefScale_ = dev->createTexture(td);
            td.debugName = "nis coef usm";
            coefUsm_ = dev->createTexture(td);
            nvrhi::CommandListHandle cl = dev->createCommandList();
            cl->open();
            cl->writeTexture(coefScale_, 0, 0, coef_scale, kFilterSize * sizeof(float));
            cl->writeTexture(coefUsm_, 0, 0, coef_usm, kFilterSize * sizeof(float));
            cl->close();
            dev->executeCommandList(cl);
        }
        return cs_ && cb_ && layout_ && pipeline_;
    }

    void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const override {
        od::renderSize(outW, outH, q, w, h);
    }

    bool temporal() const override { return false; }
    uint32_t phaseCount(int, int) const override { return 1; }

    void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) override {
        NISConfig cfg{};
        if (scaling_)
            NVScalerUpdateConfig(cfg, kSharpness, 0, 0, uint32_t(in.renderW), uint32_t(in.renderH), uint32_t(in.renderW),
                                 uint32_t(in.renderH), 0, 0, uint32_t(in.outW), uint32_t(in.outH), uint32_t(in.outW),
                                 uint32_t(in.outH));
        else
            NVSharpenUpdateConfig(cfg, kSharpness, 0, 0, uint32_t(in.renderW), uint32_t(in.renderH),
                                  uint32_t(in.renderW), uint32_t(in.renderH), 0, 0);
        if (in.color != setColor_ || in.output != setOutput_) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, cb_), nvrhi::BindingSetItem::Sampler(0, sampler_),
                           nvrhi::BindingSetItem::Texture_SRV(0, in.color),
                           nvrhi::BindingSetItem::Texture_UAV(0, in.output)};
            if (scaling_) {
                bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(1, coefScale_));
                bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(2, coefUsm_));
            }
            set_ = dev_->createBindingSet(bs, layout_);
            setColor_ = in.color;
            setOutput_ = in.output;
        }
        cl->writeBuffer(cb_, &cfg, sizeof cfg);
        nvrhi::ComputeState cs;
        cs.pipeline = pipeline_;
        cs.bindings = {set_};
        cl->setComputeState(cs);
        const uint32_t blockH = scaling_ ? kScalerBlockH : kSharpenBlockH;
        cl->dispatch((uint32_t(in.outW) + kBlockW - 1) / kBlockW, (uint32_t(in.outH) + blockH - 1) / blockH);
    }

private:
    nvrhi::IDevice* dev_ = nullptr;
    bool scaling_ = false;
    nvrhi::ShaderHandle cs_;
    nvrhi::BufferHandle cb_;
    nvrhi::SamplerHandle sampler_;
    nvrhi::BindingLayoutHandle layout_;
    nvrhi::ComputePipelineHandle pipeline_;
    nvrhi::TextureHandle coefScale_, coefUsm_;
    nvrhi::BindingSetHandle set_;
    nvrhi::ITexture* setColor_ = nullptr;
    nvrhi::ITexture* setOutput_ = nullptr;
};

} // namespace

std::unique_ptr<Upscaler> createNisUpscaler() { return std::make_unique<NisUpscaler>(); }

} // namespace od
