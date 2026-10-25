// The lock-on reticle's triangles (the Prim::front prims of game/reticle_draw), kept apart from the per-tick
// effects of scene_builder: a camera-facing 3D shape at the target point, built every rendered frame so it faces
// that frame's camera. It stays in the world (perspective size, depth tested); RenderVertex::layer pushes it toward
// the eye in the vertex shader (scene.hlsl, with the camera actually drawn) by kPush units -- along the view ray, so
// its screen position is unchanged -- which puts it in front of the target's own model around the hit point.
#include "app/play_session.hpp"

#include <cmath>

namespace od::app {

namespace {
constexpr float kPush = 256.0f;           // game units toward the eye: past the surface of a target around its point
} // namespace

std::vector<od::DynamicObject> PlayerView::reticleOverlay(const RoomMesh& mesh, const od::Camera& cam) const {
    std::vector<od::DynamicObject> out;
    if (!play || roomStage != mesh.stage || roomSub != mesh.sub || mesh.fxSlices.empty()) return out;
    const float f[3] = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};
    float r[3] = {f[2], 0.0f, -f[0]}; // LookToLH: right = up x forward
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-6f) return out;
    r[0] /= rl;
    r[2] /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]}; // fwd x right
    const float layer = kPush / od::kLayerStep;
    // the target point is in game units: the exe projects it << stage shift (0x5f2870), as the room is drawn here
    // (<< mesh.shift); the arrowheads are camera-plane offsets in projection units (live shift -> the mesh's)
    const float world = float(1 << mesh.shift), plane = liveShiftScale(mesh);
    for (const od::fx::Prim& p : fxPrims) {
        if (!p.front || !p.screen || p.line || p.texture != -1) continue; // gradient-row quads only (reticle_draw)
        const uint32_t id = od::objectId(od::kKindEffect, p.object | p.blend);
        if (out.empty() || out.back().id != id) out.push_back(od::DynamicObject{id, {}, p.blend});
        od::DynamicObject& obj = out.back();
        const float c[3] = {p.centre[0] * world, -p.centre[1] * world, p.centre[2] * world};
        od::RenderVertex v[4]{};
        for (int k = 0; k < p.n; ++k) {
            const od::fx::Vertex& s = p.v[k];
            for (int q = 0; q < 3; ++q)
                v[k].pos[q] = c[q] + ((q == 1 ? -s.woff[q] : s.woff[q]) + r[q] * s.off[0] - u[q] * s.off[1]) * plane;
            v[k].uv[0] = s.u / 256.0f;
            v[k].uv[1] = s.v / 256.0f;
            v[k].shade = s.shade;
            for (int q = 0; q < 3; ++q) v[k].color[q] = s.color[q];
            v[k].slice = float(mesh.fxGradSlice);
            v[k].cull = 0.0f;
            v[k].layer = layer;
        }
        if (p.n == 3)
            for (int k = 0; k < 3; ++k) obj.tris.push_back(v[k]);
        else
            for (int idx : od::kQuadTris) obj.tris.push_back(v[idx]);
    }
    return out;
}

} // namespace od::app
