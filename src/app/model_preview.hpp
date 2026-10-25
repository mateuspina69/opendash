// OpenDash's model pictures in the 2D screens (not the game's look): a ScreenPrim sprite with `model` set shows the
// model preview game/model_preview captured (the special weapon's turning model of the equipment page / the development room)
// rendered by the GPU (Renderer::renderPreview) at twice the box's window size -- depth buffer, light, the PS1
// textures point-sampled on smooth geometry -- and drawn into the box (the bilinear UI sampler halves it: 2x2
// supersampling).
#pragma once

#include <DirectXMath.h>
#include "render/ui_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/model_preview.hpp"
#include "engine/screen_prim.hpp"
#include "engine/vram.hpp"
#include "render/renderer.hpp"

namespace od::app {

inline void paintModelPreview(od::Renderer& r, const od::Vram& vram, ui::Canvas& cv, const od::port::ScreenPrim& p,
                              float s, ui::Vec2 o) {
    const od::port::ModelPreview& mp = od::port::modelPreview(p.model);
    if (mp.tris.empty() || p.w <= 0 || p.h <= 0) return;
    // the texture windows the model samples, stacked into one atlas (256 x 256 each)
    std::map<uint32_t, int> pages;
    for (const auto& t : mp.tris) pages.emplace(uint32_t(t.tpage & 0x19f) << 16 | t.clut, 0);
    int n = 0;
    for (auto& [key, k] : pages) k = n++;
    PreviewRequest req;
    req.atlasW = 256;
    req.atlasH = 256 * n;
    req.atlas.resize(size_t(req.atlasW) * size_t(req.atlasH));
    for (const auto& [key, k] : pages) {
        const std::vector<uint8_t> rgba = vram.decode(uint16_t(key >> 16), uint16_t(key));
        std::memcpy(&req.atlas[size_t(k) * 256 * 256], rgba.data(), std::min(rgba.size(), size_t(256 * 256 * 4)));
    }
    // view space (x right, y down, z away) -> y up; the turning model is kept centred on its mean point, whose
    // distance to every vertex does not change while it turns (the framing stays still)
    float centre[3] = {0, 0, 0};
    for (const auto& t : mp.tris)
        for (int k = 0; k < 3; ++k) {
            centre[0] += t.p[k][0];
            centre[1] -= t.p[k][1];
            centre[2] += t.p[k][2];
        }
    for (float& c : centre) c /= float(mp.tris.size() * 3);
    float radius = 1.0f;
    req.tris.reserve(mp.tris.size() * 3);
    for (const auto& t : mp.tris) {
        const int k = pages[uint32_t(t.tpage & 0x19f) << 16 | t.clut];
        for (int c = 0; c < 3; ++c) {
            PreviewVertex v{};
            v.pos[0] = t.p[c][0];
            v.pos[1] = -t.p[c][1];
            v.pos[2] = t.p[c][2];
            const float d[3] = {v.pos[0] - centre[0], v.pos[1] - centre[1], v.pos[2] - centre[2]};
            radius = std::max(radius, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
            v.uv[0] = (float(t.u[c]) + 0.25f) / 256.0f;
            v.uv[1] = (float(t.v[c]) + 0.25f + 256.0f * float(k)) / float(req.atlasH);
            for (int ch = 0; ch < 3; ++ch) v.color[ch] = float((t.colour[c] >> (ch * 8)) & 0xff) / 128.0f;
            req.tris.push_back(v);
        }
    }
    // a 3/4 view from slightly above, the bounding sphere filling the box
    using namespace DirectX;
    const float fov = XMConvertToRadians(30.0f), pitch = XMConvertToRadians(18.0f);
    const float dist = radius / std::sin(fov * 0.5f) * 1.04f;
    const XMVECTOR at = XMVectorSet(centre[0], centre[1], centre[2], 1.0f);
    const XMVECTOR eye = XMVectorAdd(at, XMVectorSet(0.0f, std::sin(pitch) * dist, -std::cos(pitch) * dist, 0.0f));
    const XMMATRIX vp = XMMatrixMultiply(XMMatrixLookAtLH(eye, at, XMVectorSet(0, 1, 0, 0)),
                                         XMMatrixPerspectiveFovLH(fov, float(p.w) / float(p.h), dist * 0.05f, dist * 3.0f));
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, vp);
    std::memcpy(req.viewProj, &m, sizeof req.viewProj);
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(req.eye), eye);
    const XMVECTOR light = XMVector3Normalize(XMVectorSet(-0.45f, 0.8f, -0.55f, 0.0f));
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(req.light), light);
    const ui::Vec2 a(o.x + float(p.x) * s, o.y + float(p.y) * s), b(o.x + float(p.x + p.w) * s, o.y + float(p.y + p.h) * s);
    const int w = std::clamp(int(std::lround((b.x - a.x) * 2.0f)), 8, 2048);
    const int h = std::clamp(int(std::lround((b.y - a.y) * 2.0f)), 8, 2048);
    const od::TextureHandle tex = r.renderPreview(p.model, w, h, req);
    if (tex) cv.image(tex, a, b, ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::rgba(255, 255, 255));
}

} // namespace od::app
