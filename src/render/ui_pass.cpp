#include "render/ui_pass.hpp"

#include <cstddef>

#include "g_ui_ps.h"
#include "g_ui_vs.h"

namespace od {

bool UiPass::init(nvrhi::IDevice* device) {
    device_ = device;
    vs_ = device->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_ui_vs, sizeof(g_ui_vs));
    ps_ = device->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_ui_ps, sizeof(g_ui_ps));
    const nvrhi::VertexAttributeDesc attrs[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(
            offsetof(ui::Vertex, pos)).setElementStride(sizeof(ui::Vertex)),
        nvrhi::VertexAttributeDesc().setName("UV").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(
            offsetof(ui::Vertex, uv)).setElementStride(sizeof(ui::Vertex)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(
            offsetof(ui::Vertex, col)).setElementStride(sizeof(ui::Vertex))};
    layout_ = device->createInputLayout(attrs, 3, vs_);

    nvrhi::BindingLayoutDesc bl;
    bl.visibility = nvrhi::ShaderType::All;
    bl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float) * 4),
                   nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
    bindingLayout_ = device->createBindingLayout(bl);
    sampler_ = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(
        nvrhi::SamplerAddressMode::Clamp));

    nvrhi::TextureDesc td; // texture 0: one white texel (untextured triangles)
    td.width = td.height = 1;
    td.format = nvrhi::Format::RGBA8_UNORM;
    td.initialState = nvrhi::ResourceStates::ShaderResource;
    td.keepInitialState = true;
    td.debugName = "ui white";
    white_ = device->createTexture(td);
    if (!(vs_ && ps_ && layout_ && bindingLayout_ && sampler_ && white_)) return false;
    const uint32_t texel = 0xffffffffu;
    nvrhi::CommandListHandle cl = device->createCommandList();
    cl->open();
    cl->writeTexture(white_, 0, 0, &texel, 4);
    cl->close();
    device->executeCommandList(cl);
    nvrhi::BindingSetDesc bs;
    bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 4),
                   nvrhi::BindingSetItem::Texture_SRV(0, white_), nvrhi::BindingSetItem::Sampler(0, sampler_)};
    whiteSet_ = device->createBindingSet(bs, bindingLayout_);
    return whiteSet_ != nullptr;
}

uint64_t UiPass::addTexture(nvrhi::ITexture* tex) {
    for (size_t i = 0; i < textures_.size(); ++i)
        if (!textures_[i]) {
            textures_[i] = tex;
            return i + 1;
        }
    textures_.push_back(tex);
    return textures_.size();
}

void UiPass::removeTexture(uint64_t id) {
    if (id == 0 || id > textures_.size()) return;
    textures_[id - 1] = nullptr;
    sets_.erase(id);
}

nvrhi::ITexture* UiPass::texture(uint64_t id) const {
    return id == 0 || id > textures_.size() ? nullptr : textures_[id - 1].Get();
}

nvrhi::IBindingSet* UiPass::bindingFor(uint64_t id) {
    if (id == 0) return whiteSet_;
    if (id > textures_.size() || !textures_[id - 1]) return nullptr;
    auto it = sets_.find(id);
    if (it != sets_.end()) return it->second;
    nvrhi::BindingSetDesc bs;
    bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 4),
                   nvrhi::BindingSetItem::Texture_SRV(0, textures_[id - 1]),
                   nvrhi::BindingSetItem::Sampler(0, sampler_)};
    return sets_.emplace(id, device_->createBindingSet(bs, bindingLayout_)).first->second;
}

void UiPass::draw(nvrhi::ICommandList* cl, nvrhi::IFramebuffer* fb, Stream stream, const ui::Vertex* v, size_t nv,
                  const uint32_t* idx, size_t ni, const ui::Cmd* cmds, size_t nc, ui::Vec2 org, ui::Vec2 size) {
    if (nv == 0 || ni == 0 || size.x <= 0.0f || size.y <= 0.0f) return;
    if (!pipeline_ || !(fb->getFramebufferInfo() == pipelineFbInfo_)) {
        nvrhi::GraphicsPipelineDesc pd;
        pd.primType = nvrhi::PrimitiveType::TriangleList;
        pd.inputLayout = layout_;
        pd.VS = vs_;
        pd.PS = ps_;
        pd.bindingLayouts = {bindingLayout_};
        pd.renderState.rasterState.setCullNone().setScissorEnable(true);
        pd.renderState.depthStencilState.depthTestEnable = false;
        pd.renderState.depthStencilState.depthWriteEnable = false;
        auto& b = pd.renderState.blendState.targets[0];
        b.blendEnable = true;
        b.srcBlend = nvrhi::BlendFactor::SrcAlpha;
        b.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        b.srcBlendAlpha = nvrhi::BlendFactor::One;
        b.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
        pipelineFbInfo_ = fb->getFramebufferInfo();
        pipeline_ = device_->createGraphicsPipeline(pd, pipelineFbInfo_);
    }

    const uint64_t vBytes = uint64_t(nv) * sizeof(ui::Vertex), iBytes = uint64_t(ni) * sizeof(uint32_t);
    auto grow = [&](nvrhi::BufferHandle& buf, uint64_t bytes, bool index) {
        if (buf && buf->getDesc().byteSize >= bytes) return;
        nvrhi::BufferDesc bd;
        bd.byteSize = bytes + bytes / 2 + 1024;
        bd.isVertexBuffer = !index;
        bd.isIndexBuffer = index;
        bd.initialState = index ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
        bd.keepInitialState = true;
        bd.debugName = index ? "ui indices" : "ui vertices";
        buf = device_->createBuffer(bd);
    };
    nvrhi::BufferHandle& vb = vb_[stream];
    nvrhi::BufferHandle& ib = ib_[stream];
    grow(vb, vBytes, false);
    grow(ib, iBytes, true);
    cl->writeBuffer(vb, v, vBytes);
    cl->writeBuffer(ib, idx, iBytes);

    const nvrhi::FramebufferInfoEx& fi = fb->getFramebufferInfo();
    const float fbW = float(fi.width), fbH = float(fi.height);
    const float sx = fbW / size.x, sy = fbH / size.y; // display -> framebuffer pixels
    const float xf[4] = {2.0f / size.x, -2.0f / size.y, -1.0f - 2.0f * org.x / size.x, 1.0f + 2.0f * org.y / size.y};
    nvrhi::GraphicsState gs;
    gs.pipeline = pipeline_;
    gs.framebuffer = fb;
    gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vb).setSlot(0).setOffset(0)};
    gs.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(ib).setFormat(nvrhi::Format::R32_UINT);
    for (size_t k = 0; k < nc; ++k) {
        const ui::Cmd& c = cmds[k];
        if (c.count == 0) continue;
        const float x0 = (c.clip[0] - org.x) * sx, y0 = (c.clip[1] - org.y) * sy;
        const float x1 = (c.clip[2] - org.x) * sx, y1 = (c.clip[3] - org.y) * sy;
        if (x1 <= x0 || y1 <= y0) continue;
        nvrhi::IBindingSet* set = bindingFor(c.tex);
        if (!set) continue;
        gs.bindings = {set};
        gs.viewport = nvrhi::ViewportState().addViewport(nvrhi::Viewport(fbW, fbH)).addScissorRect(
            nvrhi::Rect(int(x0), int(x1), int(y0), int(y1)));
        cl->setGraphicsState(gs);
        cl->setPushConstants(xf, sizeof xf);
        cl->drawIndexed(nvrhi::DrawArguments().setVertexCount(c.count).setStartIndexLocation(c.first));
    }
}

} // namespace od
