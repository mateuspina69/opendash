#include "imgui_nvrhi.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstring>

namespace od {

static_assert(sizeof(ImDrawVert) == sizeof(ui::Vertex) && offsetof(ImDrawVert, uv) == offsetof(ui::Vertex, uv) &&
                  offsetof(ImDrawVert, col) == offsetof(ui::Vertex, col),
              "ImDrawVert is copied as ui::Vertex");

void ImGuiNvrhi::init(UiPass* pass) {
    pass_ = pass;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "opendash_nvrhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
}

// ImGui 1.92 texture requests: whole-texture uploads (the atlas changes rarely)
void ImGuiNvrhi::updateTexture(nvrhi::ICommandList* cl, ImTextureData* t) {
    if (t->Status == ImTextureStatus_WantDestroy) {
        pass_->removeTexture(t->GetTexID());
        t->SetTexID(ImTextureID_Invalid);
        t->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (t->Status != ImTextureStatus_WantCreate && t->Status != ImTextureStatus_WantUpdates) return;
    std::vector<uint32_t> rgba(size_t(t->Width) * size_t(t->Height));
    if (t->Format == ImTextureFormat_RGBA32) {
        std::memcpy(rgba.data(), t->GetPixels(), rgba.size() * 4);
    } else { // Alpha8 -> white with alpha
        const auto* a = static_cast<const uint8_t*>(t->GetPixels());
        for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = uint32_t(a[i]) << 24 | 0x00ffffffu;
    }
    if (t->Status == ImTextureStatus_WantCreate) {
        nvrhi::TextureDesc td;
        td.width = uint32_t(t->Width);
        td.height = uint32_t(t->Height);
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::ShaderResource;
        td.keepInitialState = true;
        td.debugName = "imgui texture";
        t->SetTexID(pass_->addTexture(cl->getDevice()->createTexture(td)));
    }
    cl->writeTexture(pass_->texture(t->GetTexID()), 0, 0, rgba.data(), size_t(t->Width) * 4);
    t->SetStatus(ImTextureStatus_OK);
}

void ImGuiNvrhi::render(nvrhi::ICommandList* cl, nvrhi::IFramebuffer* fb, ImDrawData* data) {
    if (data->Textures)
        for (ImTextureData* t : *data->Textures) updateTexture(cl, t);
    if (data->TotalVtxCount == 0) return;
    // one list: the indices rebased on the concatenated vertices
    verts_.clear();
    idx_.clear();
    cmds_.clear();
    for (const ImDrawList* l : data->CmdLists) {
        const uint32_t vb = uint32_t(verts_.size());
        verts_.resize(verts_.size() + size_t(l->VtxBuffer.Size));
        std::memcpy(&verts_[vb], l->VtxBuffer.Data, size_t(l->VtxBuffer.Size) * sizeof(ImDrawVert));
        for (const ImDrawCmd& c : l->CmdBuffer) {
            if (c.UserCallback) continue; // no callbacks used by opendash
            const ImTextureID tex = c.GetTexID();
            if (!tex) continue; // ImGui never draws untextured (its atlas holds the white texel)
            ui::Cmd u;
            u.tex = tex;
            u.clip[0] = c.ClipRect.x;
            u.clip[1] = c.ClipRect.y;
            u.clip[2] = c.ClipRect.z;
            u.clip[3] = c.ClipRect.w;
            u.first = uint32_t(idx_.size());
            u.count = c.ElemCount;
            for (uint32_t k = 0; k < c.ElemCount; ++k)
                idx_.push_back(vb + c.VtxOffset + uint32_t(l->IdxBuffer[int(c.IdxOffset + k)]));
            cmds_.push_back(u);
        }
    }
    pass_->draw(cl, fb, UiPass::DevPanels, verts_.data(), verts_.size(), idx_.data(), idx_.size(), cmds_.data(),
                cmds_.size(), {data->DisplayPos.x, data->DisplayPos.y}, {data->DisplaySize.x, data->DisplaySize.y});
}

} // namespace od
