// Temporal AA / upscaler backends behind one interface (DLSS, FSR, XeSS, NIS, SSAA, MSAA, none). See ADR 0004.
#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

#include "renderer.hpp"

namespace od {

struct UpscalerInputs {
    nvrhi::ITexture* color;    // render size, RGBA16F, jittered
    nvrhi::ITexture* depth;    // render size, R32F copy of the D24 depth, standard Z (1 = far)
    nvrhi::ITexture* motion;   // render size, RG16F, pixels, prev - cur
    nvrhi::ITexture* reactive; // render size, R8
    nvrhi::ITexture* output;   // output size, RGBA16F, UAV
    float jitterX, jitterY;    // pixels, as applied to the projection
    int renderW, renderH, outW, outH;
    float frameTimeMs;
    float nearZ, farZ, fovY;
    bool reset; // discontinuity: drop the history
};

class Upscaler {
public:
    virtual ~Upscaler() = default;
    virtual AaBackend backend() const = 0;
    virtual bool init(nvrhi::IDevice* dev, int outW, int outH, AaQuality q) = 0; // false = unavailable
    virtual void renderSize(int outW, int outH, AaQuality q, int& w, int& h) const = 0;
    virtual bool temporal() const = 0; // false: no jitter (none, NIS)
    virtual uint32_t phaseCount(int renderW, int outW) const = 0;
    virtual void evaluate(nvrhi::ICommandList* cl, const UpscalerInputs& in) = 0;
    // a run-time failure (feature creation / dispatch): the backend copied the colour this frame and the renderer
    // switches to the next candidate
    virtual bool failed() const { return false; }
    virtual int sampleCount() const { return 1; } // > 1: the scene targets are multisampled (MSAA)
};

// every backend is built in; init() says whether it runs here (its DLL, the GPU)
std::unique_ptr<Upscaler> createUpscaler(AaBackend b);

} // namespace od
