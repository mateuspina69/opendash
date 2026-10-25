// Dear ImGui (the F1 developer panels only) on the 2D pass (ui_pass.hpp): draw lists, ImGui 1.92 texture requests;
// ImTextureID = a UiPass texture id.
#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <vector>

#include "render/ui_pass.hpp"

struct ImDrawData;
struct ImTextureData;

namespace od {

class ImGuiNvrhi {
public:
    void init(UiPass* pass); // also sets the ImGui renderer backend flags
    // records into cl (open) on fb; uploads pending ImGui texture requests first
    void render(nvrhi::ICommandList* cl, nvrhi::IFramebuffer* fb, ImDrawData* data);

private:
    void updateTexture(nvrhi::ICommandList* cl, ImTextureData* t);

    UiPass* pass_ = nullptr;
    std::vector<ui::Vertex> verts_;
    std::vector<uint32_t> idx_;
    std::vector<ui::Cmd> cmds_;
};

} // namespace od
