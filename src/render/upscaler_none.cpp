// No anti-aliasing: render size = output size, the scene colour is copied as is. Also the backend factory.
#include "upscaler.hpp"

namespace od {

std::unique_ptr<Upscaler> createFsrUpscaler();
std::unique_ptr<Upscaler> createDlssUpscaler();
std::unique_ptr<Upscaler> createXessUpscaler();
std::unique_ptr<Upscaler> createNisUpscaler();

namespace {

class NoneUpscaler final : public Upscaler {
public:
    AaBackend backend() const override { return AaBackend::None; }
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
};

} // namespace

#ifndef _WIN32
// the vendor SDKs are built for D3D12 only (third_party/): outside Windows the backends are not compiled in, but FSR
// (upscaler_fsr_vk.cpp, when cmake/fidelityfx_vk.cmake finds the SDK)
#ifndef OD_FSR_VK
std::unique_ptr<Upscaler> createFsrUpscaler() { return nullptr; }
#endif
std::unique_ptr<Upscaler> createDlssUpscaler() { return nullptr; }
std::unique_ptr<Upscaler> createXessUpscaler() { return nullptr; }
std::unique_ptr<Upscaler> createNisUpscaler() { return nullptr; }
#endif

std::unique_ptr<Upscaler> createUpscaler(AaBackend b) {
    switch (b) {
    case AaBackend::None: return std::make_unique<NoneUpscaler>();
    case AaBackend::Fsr: return createFsrUpscaler();
    case AaBackend::Dlss: return createDlssUpscaler();
    case AaBackend::Xess: return createXessUpscaler();
    case AaBackend::Nis: return createNisUpscaler();
    default: return nullptr;
    }
}

} // namespace od
