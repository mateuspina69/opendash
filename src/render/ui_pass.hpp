// The 2D pass on NVRHI: the UI textures (Renderer::createTexture ids) and the drawing of triangle lists in window
// pixels -- the game's canvas (render/ui_canvas.hpp) and the F1 panels' ImGui draw data (imgui_nvrhi.cpp) both go
// through it. Alpha blended, no depth, scissor per command; texture 0 = white (untextured).
#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "render/ui_canvas.hpp"

namespace od {

class UiPass {
public:
    // the stream a draw uses (its own vertex / index buffers: several streams are drawn in one command list)
    enum Stream { Game, DevPanels, kStreams };

    bool init(nvrhi::IDevice* device);
    uint64_t addTexture(nvrhi::ITexture* tex); // -> id (>= 1)
    void removeTexture(uint64_t id);
    nvrhi::ITexture* texture(uint64_t id) const;
    // records into cl (open) on fb: the vertices are in pixels of a display w x h at origin `org`
    void draw(nvrhi::ICommandList* cl, nvrhi::IFramebuffer* fb, Stream stream, const ui::Vertex* v, size_t nv,
              const uint32_t* idx, size_t ni, const ui::Cmd* cmds, size_t nc, ui::Vec2 org, ui::Vec2 size);

private:
    nvrhi::IBindingSet* bindingFor(uint64_t id);

    nvrhi::IDevice* device_ = nullptr;
    nvrhi::ShaderHandle vs_, ps_;
    nvrhi::InputLayoutHandle layout_;
    nvrhi::BindingLayoutHandle bindingLayout_;
    nvrhi::SamplerHandle sampler_;
    nvrhi::GraphicsPipelineHandle pipeline_;
    nvrhi::FramebufferInfo pipelineFbInfo_;
    nvrhi::TextureHandle white_;
    nvrhi::BindingSetHandle whiteSet_;
    nvrhi::BufferHandle vb_[kStreams], ib_[kStreams];
    std::vector<nvrhi::TextureHandle> textures_; // id - 1
    std::unordered_map<uint64_t, nvrhi::BindingSetHandle> sets_;
};

} // namespace od
