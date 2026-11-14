// NVRHI renderer (D3D12 on Windows, Vulkan elsewhere).
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cfloat>
#include <map>
#include <chrono>
#include <cmath>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>

#include "engine/texture_pack.hpp"
#ifdef _WIN32
#include "device_d3d12.hpp"
#else
#include "device_vulkan.hpp"
#endif
#include "imgui_nvrhi.hpp"
#include "render/ui_canvas.hpp"
#include "render/ui_pass.hpp"
#include "render_logic.hpp"
#include "renderer.hpp"
#include "upscaler.hpp"

#include "g_blizzard_ps.h"
#include "g_bloom_down_ps.h"
#include "g_bloom_up_ps.h"
#include "g_debugview_ms_ps.h"
#include "g_debugview_ps.h"
#include "g_debugview_vs.h"
#include "g_flake_ps.h"
#include "g_flake_vs.h"
#include "g_full_vs.h"
#include "g_height_ps.h"
#include "g_height_vs.h"
#include "g_light_blur_x_ps.h"
#include "g_light_blur_y_ps.h"
#include "g_light_ps.h"
#include "g_outline_ms_ps.h"
#include "g_outline_ps.h"
#include "g_output_ps.h"
#include "g_output_vs.h"
#include "g_preview_ps.h"
#include "g_preview_vs.h"
#include "g_scene_pre_ps.h"
#include "g_scene_ps.h"
#include "g_scene_rt_ps.h"
#include "g_scene_vs.h"
#include "g_subdivide_cs.h"

using namespace DirectX;

namespace od {

namespace {

// RenderVertex + what the GPU derives: shading normal, the position one frame ago (motion vectors) and, for the quad
// culling rules (RenderVertex::cull 2..5), the winding normals of the own triangle and of the quad's other half
struct GpuVertex {
    RenderVertex v;
    float normal[3];
    float prevPos[3];
    float faceN[3];
    float pairN[3];
    float surface; // bits 0..3: kSurfaceModel a model's face (actors, the player, bank models), clipped at the game's
                   // near plane; kSurfaceNearFade a world effect, fading toward the camera (setEffectNearFade);
                   // kSurfaceEffect an effect (particles, billboards, rings): its blended texels fade at the scenery;
                   // kSurfaceGlow an effect that gives light (DynamicObject::glow): the bloom's source.
                   // Above them (* kSurfacePiece): RenderVertex::piece, the see-through's scenery piece
    float uvRect[4]; // the triangle's UVs' bounds (min u, v, max u, v): what the page filter may read of its page
    float object;    // the dynamic object it belongs to, 1 on in the frame's order (uploadDynamic); 0: the room, the sky
    float pieceN[3]; // the anime look's shading normal (pieceNormals), not unit: linear over a face, so a draw's and the
                     // subdivision's interpolation of it are exact; 0: none (the shading normal above)
};
static_assert(sizeof(RenderVertex) == 52 && sizeof(GpuVertex) == 136);
constexpr uint32_t kSurfaceModel = 1, kSurfaceNearFade = 2, kSurfaceEffect = 4, kSurfaceGlow = 8;
constexpr float kSurfacePiece = 16.0f;
// the instances' ray masks: the sun's rays meet both; a particle light's rays each its own stretch (scene.hlsl)
constexpr uint8_t kRayMaskRoom = 1, kRayMaskActors = 2;

// scene.hlsl cbuffer Frame
struct FrameConstants {
    XMFLOAT4X4 viewProj, viewProjNoJit, prevViewProj;
    float eye[3];
    float cullSign;
    float prevEye[3];
    float fogStart;
    float fwd[3];
    float fogEnd;
    float fogColor[3];
    float reactive;
    float renderSize[2];
    float motionMode;
    float blendMode; // the draw's DynamicObject::blend (0 opaque): fog fades additive / multiplied light out
    float sunDir[3];
    float sunStrength;
    float shadowMode;
    float semiPass; // a semi-transparent range: 1 its opaque texels (no STP bit), 2 its blended ones; 0 any draw else
    float modelNear; // the game's near plane (world units): a model's face is clipped there (scene.hlsl)
    float seeLens; // 1: the see-through's lens pass (its open pieces drawn as a lens); 0 any draw else
    float fogMode; // 0 off, 1 classic (linear on depth), 2 remaster (exponential height fog), 3 the PS1's depth cue
    float fogDensity;
    float fogHeightFalloff;
    float fogMaxOpacity;
    float sunScatter; // remaster fog brightened toward the sun (0 = sun off)
    float fogSky;     // remaster fog of a sky draw (the ray never ends): 0 not sky, 1 dome, 2 screen-space layer
    float seeClock;   // the see-through lens's clock (seconds)
    float pageBias; // the page filter's mip bias: log2(render size / output size)
    float water[4];     // remaster medium look: on, clock (seconds), sky draw, kind (0 water, 1 hot air)
    float waterTint[4]; // the room's water colour
    XMFLOAT4X4 skyRay;  // screen-space sky: NDC -> view direction (inverse of the unjittered rotation-only viewProj)
    float seeBody[kMaxSeeThroughBodies][4];  // see-through: a body's hole on screen (NDC centre x, y, radius x, y)
    float seeInfo[4];                        // ... the bodies' nearest view depths (x, y), the open pieces (z), debug (w)
    float seePiece[kMaxSeeThroughPieces][4]; // ... an open piece: its RenderVertex::piece, opening, whole (1), bodies
    float heightBounds[4]; // precipitation height map: min x, min z, 1 / its width, 1 / its depth
    float heightRange[4];  // ... its top y, its height span, its size in texels (x, z)
    float precip[4];       // Precipitation: snow, blizzard, gust, seconds
    float precipWind[4];   // ... wind (world units a second), unit
    float precipView[4];   // ... tan(fovY / 2), the flakes' light
    XMFLOAT4X4 invViewProj; // the blown snow's pass: NDC -> world (jittered)
    float softDepth[4];     // the depth copy's near / far planes (its raw depth -> view depth: the soft effects);
                            // the effects' near fade (setEffectNearFade): gone at z, whole from w
    float precipLook[8];    // Precipitation's look: density, size, brightness, swirl / flutter, reach, streak, chaos
    float precipLayer[4];   // the flake field drawn (drawScene): its cells' scale, flakes a cell, 1 = the far field
    float skyPoleUp[4];     // the dome's poles (SkyPoles): the colour, a = where the fade starts (a sine)
    float skyPoleDown[4];   // ... a = where it is whole (a sine); 0: a draw that is not the dome, or no fade
    float lightPos[kMaxParticleLights][4];   // the particle lights: position, radius
    float lightColor[kMaxParticleLights][4]; // ... colour
    float lightInfo[4];                      // ... how many this draw receives, the gain
    float lightTerms[4]; // the traced light on this draw: the sun's visibility from it, the occlusion's strength, the
                         // shadows' colour; 1 = its glowing effects' light goes to the glow target
    float lightTrace[4]; // ps_light: tan of the sun's angular radius (< 0 no ray), the occlusion rays' reach, how many,
                         // the sample pattern's turn
    float toon[4];       // the anime look: how much deeper a model's dark band is, its rim light, 1 = on
    float outline[4];    // ps_outline: the line's width in pixels, its strength, 1 = the scenery too, the depth it is gone at
    float outlineLook[4]; // ... how much of the colour of what it goes around it takes
    float sunDisc[4];     // the sun in the sky (SunLight::disc): its angular radius (radians), the sky draw (1 dome, 2
                          // screen-space layer; 0 none)
};
// bloom.hlsl Params
struct BloomConstants {
    float target[2]; // 1 / the target's size
    float texel[2];  // the source's texel
    float gain;
    float pad[3];
};
constexpr int kBloomLevels = 5; // halved copies of the glow target
// scene.hlsl vs_flake's two fields of kFlakeCells (24 x 12 x 24) cells, 6 vertices a flake: the near one (up to
// kFlakesNear a cell) and the far one beyond it (cells kFlakeFarScale times larger, up to kFlakesFar a cell)
constexpr uint32_t kFlakeCellCount = 24 * 12 * 24, kFlakesNear = 32, kFlakesFar = 16;
constexpr float kFlakeFarScale = 3.0f;
constexpr int kHeightTexels = 1024; // the height map's longer side

// Same projection and 24-bit depth as the former D3D11 renderer: the room data has coplanar faces
// whose depths differ only by rounding (different quad splits). D24 quantisation makes them tie, and LESS keeps
// the first drawn, like the original; reverse-Z on D32F resolves those ties per pixel and shows stripes
// (ST12 sub 1 floor).
// Vulkan has no D24 on AMD: D32 there, with the scene shader writing its depth at D24's precision (the same ties)
#ifdef _WIN32
constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D24S8;
#else
constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;
#endif
constexpr bool kDepthHasStencil = kDepthFormat == nvrhi::Format::D24S8;
constexpr nvrhi::Format kGlowFormat = nvrhi::Format::R11G11B10_FLOAT; // the glow target and the bloom's copies
constexpr float kFar = 200000.0f;

// jitter in pixels -> NDC offset (row-major: row 2 is multiplied by view z, so the offset scales with w)
XMMATRIX withJitter(XMMATRIX p, float jx, float jy, int rw, int rh) {
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, p);
    m._31 += 2.0f * jx / float(rw);
    m._32 -= 2.0f * jy / float(rh);
    return XMLoadFloat4x4(&m);
}

// smoothRanges: the vertex ranges [first, end) of the objects whose normals are averaged over the faces sharing a
// position inside the same object (actors and the room: the sun's light varies across the low-poly surface instead of
// per face); creases sharper than ~60 degrees and faces wound the other way stay apart. Every other vertex (effects:
// particles / rings / billboards, many of them starting on one point) keeps its flat face normal -- averaged over the
// whole buffer, an effect's hundreds of coincident corners made this quadratic (the ST4B sub 5 clash: 1.5 s a frame)
// and one object took its neighbours' normals.
// GpuVertex::uvRect of the triangle whose first vertex is `first`
void setUvRect(GpuVertex* first) {
    const float* a = first[0].v.uv;
    const float* b = first[1].v.uv;
    const float* c = first[2].v.uv;
    const float rect[4] = {std::min({a[0], b[0], c[0]}), std::min({a[1], b[1], c[1]}), std::max({a[0], b[0], c[0]}),
                           std::max({a[1], b[1], c[1]})};
    for (int k = 0; k < 3; ++k) std::memcpy(first[k].uvRect, rect, sizeof rect);
}

// a position's key: the corners that meet at a point (a quarter of a world unit)
uint64_t positionKey(const float* p) {
    const auto q = [](float v) { return uint64_t(int64_t(std::lround(v * 4.0f)) & 0x1FFFFF); };
    return q(p[0]) | q(p[1]) << 21 | q(p[2]) << 42;
}

// The shading normals of a model of the anime look (the vertices [first, end) of one object), into GpuVertex::pieceN.
// A PS1 model is made of rigid pieces, each a mesh of its own (the faces that share corners). A piece is shaded as the
// solid it stands for, not face by face: a corner's normal points out of the piece's ellipsoid of inertia (its
// surface's covariance, inverted twice: what lies along a long piece hardly counts), so around a limb it turns as
// around a cylinder and along it it stays the same -- the border between the light's bands runs the piece's whole
// length, where the faces' averaged normals differed between a face's own corners and the border cut that face at a
// slant, another way with every pose. The normal is linear in the position (one scale a piece), so it is kept not
// unit. A face the piece's normal is nearly tangent to (a limb's end cap, a flat sheet) and a piece of fewer than four
// faces have none.
void pieceNormals(const std::vector<RenderVertex>& tris, size_t first, size_t end, const float* fn, GpuVertex* out) {
    const size_t base = first / 3, faces = (end - first) / 3;
    if (faces == 0) return;
    std::vector<uint32_t> piece(faces);
    for (size_t f = 0; f < faces; ++f) piece[f] = uint32_t(f);
    const auto root = [&piece](uint32_t a) {
        while (piece[a] != a) a = piece[a] = piece[piece[a]];
        return a;
    };
    std::unordered_map<uint64_t, uint32_t> met; // a position -> a face that has it
    for (size_t k = first; k < first + faces * 3; ++k) {
        const uint32_t f = uint32_t(k / 3 - base);
        const auto [it, fresh] = met.try_emplace(positionKey(tris[k].pos), f);
        if (!fresh) piece[root(f)] = root(it->second);
    }
    struct Moments { // of a piece's surface, around the object's first vertex
        double area = 0.0, sum[3]{}, square[6]{}; // xx xy xz yy yz zz
        uint32_t faces = 0;
        double centre[3]{}, twice[9]{}; // the centre; the covariance inverted twice (0: no solid)
        double scale = 1.0;             // what brings twice * (a corner - the centre) near 1
        bool solid = false;
    };
    std::unordered_map<uint32_t, Moments> moments;
    const float* origin = tris[first].pos;
    static constexpr int kPair[6][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}};
    for (size_t f = 0; f < faces; ++f) {
        double p[3][3], g[3] = {0.0, 0.0, 0.0};
        for (int v = 0; v < 3; ++v)
            for (int c = 0; c < 3; ++c) {
                p[v][c] = double(tris[first + f * 3 + size_t(v)].pos[c]) - double(origin[c]);
                g[c] += p[v][c] / 3.0;
            }
        const double u[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const double w[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const double cr[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
        const double area = 0.5 * std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        Moments& m = moments[root(uint32_t(f))];
        ++m.faces;
        m.area += area;
        for (int c = 0; c < 3; ++c) m.sum[c] += area * g[c];
        for (int q = 0; q < 6; ++q) { // a triangle's second moment: area / 12 * (its corners' + 9 * its centre's)
            const int a = kPair[q][0], b = kPair[q][1];
            m.square[q] += area / 12.0 * (p[0][a] * p[0][b] + p[1][a] * p[1][b] + p[2][a] * p[2][b] + 9.0 * g[a] * g[b]);
        }
    }
    for (auto& [id, m] : moments) {
        if (m.faces < 4 || m.area <= 1e-9) continue;
        for (int c = 0; c < 3; ++c) m.centre[c] = m.sum[c] / m.area;
        double cov[3][3];
        for (int q = 0; q < 6; ++q) {
            const int a = kPair[q][0], b = kPair[q][1];
            cov[a][b] = cov[b][a] = m.square[q] / m.area - m.centre[a] * m.centre[b];
        }
        // a flat piece has no extent across: its normal there is kept finite
        const double floor = 0.02 * (cov[0][0] + cov[1][1] + cov[2][2]) / 3.0;
        if (floor <= 1e-12) continue;
        for (int c = 0; c < 3; ++c) cov[c][c] += floor;
        double inv[3][3];
        inv[0][0] = cov[1][1] * cov[2][2] - cov[1][2] * cov[2][1];
        inv[0][1] = cov[0][2] * cov[2][1] - cov[0][1] * cov[2][2];
        inv[0][2] = cov[0][1] * cov[1][2] - cov[0][2] * cov[1][1];
        inv[1][1] = cov[0][0] * cov[2][2] - cov[0][2] * cov[2][0];
        inv[1][2] = cov[0][2] * cov[1][0] - cov[0][0] * cov[1][2];
        inv[2][2] = cov[0][0] * cov[1][1] - cov[0][1] * cov[1][0];
        const double det = cov[0][0] * inv[0][0] + cov[0][1] * inv[0][1] + cov[0][2] * inv[0][2];
        if (std::abs(det) <= 1e-30) continue;
        inv[1][0] = inv[0][1];
        inv[2][0] = inv[0][2];
        inv[2][1] = inv[1][2];
        for (auto& row : inv)
            for (double& v : row) v /= det;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                m.twice[a * 3 + b] = inv[a][0] * inv[0][b] + inv[a][1] * inv[1][b] + inv[a][2] * inv[2][b];
        const double spread = (cov[0][0] + cov[1][1] + cov[2][2]) / 3.0;
        m.scale = spread * std::sqrt(spread);
        m.solid = true;
    }
    for (size_t f = 0; f < faces; ++f) {
        const Moments& m = moments[root(uint32_t(f))];
        if (!m.solid) continue;
        float raw[3][3];
        double mean[3] = {0.0, 0.0, 0.0};
        bool whole = true;
        for (int v = 0; v < 3 && whole; ++v) {
            const float* pos = tris[first + f * 3 + size_t(v)].pos;
            double d[3], o[3];
            for (int c = 0; c < 3; ++c) d[c] = double(pos[c]) - double(origin[c]) - m.centre[c];
            for (int c = 0; c < 3; ++c) o[c] = m.twice[c * 3] * d[0] + m.twice[c * 3 + 1] * d[1] + m.twice[c * 3 + 2] * d[2];
            const double len = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            whole = len > 1e-30;
            for (int c = 0; c < 3 && whole; ++c) {
                raw[v][c] = float(o[c] * m.scale);
                mean[c] += o[c] / len;
            }
        }
        if (!whole) continue;
        const float* own = &fn[(base + f) * 3];
        const double len = std::sqrt(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]);
        if (len <= 1e-9 || std::abs(mean[0] * own[0] + mean[1] * own[1] + mean[2] * own[2]) / len < 0.2) continue;
        for (size_t v = 0; v < 3; ++v) std::memcpy(out[first + f * 3 + v].pieceN, raw[v], sizeof raw[v]);
    }
}

// pieceRanges: the vertex ranges of the models of the anime look (pieceNormals), among smoothRanges'
void toGpu(const std::vector<RenderVertex>& tris, const float* prevPos, std::vector<GpuVertex>& out,
           const std::vector<std::pair<size_t, size_t>>* smoothRanges = nullptr,
           const std::vector<std::pair<size_t, size_t>>* surfaceRanges = nullptr,
           const std::vector<uint32_t>* surfaceBits = nullptr,
           const std::vector<std::pair<size_t, size_t>>* pieceRanges = nullptr) {
    out.resize(tris.size());
    const size_t faces = tris.size() / 3;
    std::vector<float> fn(faces * 3);
    for (size_t f = 0; f < faces; ++f) faceNormal(tris[f * 3].pos, tris[f * 3 + 1].pos, tris[f * 3 + 2].pos, &fn[f * 3]);
    std::vector<float> nrm(fn.size() * 3); // per vertex
    for (size_t k = 0; k < faces * 3; ++k) std::memcpy(&nrm[k * 3], &fn[k / 3 * 3], 3 * sizeof(float));
    const auto key = positionKey;
    if (smoothRanges) {
        // The corners that meet at a point fall in surfaces: two faces are of one surface when they meet at less than
        // ~60 degrees, and so on from face to face around the point (a crease or a face wound the other way starts
        // another). Every corner of a surface takes the same normal there, its faces' weighted by their angles at the
        // point: the normal is continuous from a face to its neighbour (a light's border runs on across their edge), and
        // how a quad is split in two triangles does not turn it. (Each corner averaging the faces within 60 degrees of
        // its own gave neighbours different normals at the point they share: the border broke off at their edge.)
        constexpr size_t kMostCorners = 256; // a model fallen on one point has no surface there: its faces' own normals
        std::vector<uint32_t> group;
        std::vector<float> sum;
        for (const auto& [first, end] : *smoothRanges) {
            std::unordered_map<uint64_t, std::vector<uint32_t>> shared; // quantised position -> the model's corners there
            for (size_t k = first; k < end; ++k) shared[key(tris[k].pos)].push_back(uint32_t(k));
            for (const auto& [position, corners] : shared) {
                const size_t count = corners.size();
                if (count < 2 || count > kMostCorners) continue;
                group.resize(count);
                for (size_t a = 0; a < count; ++a) group[a] = uint32_t(a);
                const auto root = [&group](uint32_t a) {
                    while (group[a] != a) a = group[a] = group[group[a]];
                    return a;
                };
                for (size_t a = 0; a < count; ++a)
                    for (size_t b = a + 1; b < count; ++b) {
                        const float* p = &fn[corners[a] / 3 * 3];
                        const float* q = &fn[corners[b] / 3 * 3];
                        if (p[0] * q[0] + p[1] * q[1] + p[2] * q[2] >= 0.5f) group[root(uint32_t(a))] = root(uint32_t(b));
                    }
                sum.assign(count * 3, 0.0f);
                for (size_t a = 0; a < count; ++a) {
                    const size_t k = corners[a], f = k / 3 * 3;
                    const float* p0 = tris[k].pos;
                    const float* p1 = tris[f + (k + 1) % 3].pos;
                    const float* p2 = tris[f + (k + 2) % 3].pos;
                    const float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                    const float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                    const float lengths = std::sqrt((u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) * (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
                    if (lengths <= 1e-12f) continue; // a degenerate corner
                    const float angle = std::acos(std::clamp((u[0] * v[0] + u[1] * v[1] + u[2] * v[2]) / lengths, -1.0f, 1.0f));
                    const size_t r = root(uint32_t(a));
                    for (size_t c = 0; c < 3; ++c) sum[r * 3 + c] += fn[f + c] * angle;
                }
                for (size_t a = 0; a < count; ++a) {
                    const float* n = &sum[size_t(root(uint32_t(a))) * 3];
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (len <= 1e-6f) continue; // (its face's own normal)
                    for (size_t c = 0; c < 3; ++c) nrm[size_t(corners[a]) * 3 + c] = n[c] / len;
                }
            }
        }
    }
    for (size_t k = 0; k < faces * 3; ++k) {
        const float* own = &fn[k / 3 * 3];
        const float* n = &nrm[k * 3];
        out[k].v = tris[k];
        std::memcpy(out[k].normal, n, 3 * sizeof(float));
        const size_t f = k / 3, c = size_t(std::lround(tris[k].cull));
        const size_t pf = (c == 2 || c == 4) && f + 1 < faces ? f + 1 : (c == 3 || c == 5) && f > 0 ? f - 1 : f;
        std::memcpy(out[k].faceN, own, sizeof out[k].faceN);
        std::memcpy(out[k].pairN, &fn[pf * 3], sizeof out[k].pairN);
        std::memcpy(out[k].prevPos, prevPos ? prevPos + k * 3 : tris[k].pos, sizeof out[k].prevPos);
        out[k].surface = tris[k].piece * kSurfacePiece;
        out[k].object = 0.0f;
        std::memset(out[k].pieceN, 0, sizeof out[k].pieceN);
    }
    for (size_t f = 0; f < faces; ++f) setUvRect(&out[f * 3]);
    if (pieceRanges)
        for (const auto& [first, end] : *pieceRanges) pieceNormals(tris, first, end, fn.data(), out.data());
    if (surfaceRanges)
        for (size_t r = 0; r < surfaceRanges->size(); ++r)
            for (size_t k = (*surfaceRanges)[r].first; k < (*surfaceRanges)[r].second; ++k)
                out[k].surface += float((*surfaceBits)[r]);
}

// subdivide.hlsl's word about a triangle that goes in: split it (a model with a plan), its round edges (bit e of the
// three after kTriSplit: SubdivisionPlan::roundEdges), and how its small triangles are culled (subdividedCull)
constexpr uint32_t kTriSplit = 1, kTriRoundShift = 1, kTriCullOne = 16, kTriReverse = 32;

// ray-tracing attributes per vertex: (u, v, slice, object) -- what the shadow rays' alpha test reads, and whose face it is
void toAttr(const std::vector<GpuVertex>& v, uint32_t count, std::vector<float>& out) {
    out.assign(size_t(std::max<uint32_t>(count, 1)) * 4, 0.0f);
    for (uint32_t i = 0; i < count; ++i) {
        out[i * 4 + 0] = v[i].v.uv[0];
        out[i * 4 + 1] = v[i].v.uv[1];
        out[i * 4 + 2] = v[i].v.slice;
        out[i * 4 + 3] = v[i].object;
    }
}

// triangle list at the start of vb (position = first 3 floats of GpuVertex), indexed 0..n-1 through ib (NVRHI's
// BLAS build requires an index buffer); not opaque: candidate hits run the alpha test
nvrhi::rt::GeometryDesc triangleGeometry(nvrhi::IBuffer* vb, nvrhi::IBuffer* ib, uint32_t vertexCount) {
    nvrhi::rt::GeometryTriangles t;
    t.setVertexBuffer(vb)
        .setVertexFormat(nvrhi::Format::RGB32_FLOAT)
        .setVertexStride(sizeof(GpuVertex))
        .setVertexCount(vertexCount)
        .setIndexBuffer(ib)
        .setIndexFormat(nvrhi::Format::R32_UINT)
        .setIndexCount(vertexCount);
    return nvrhi::rt::GeometryDesc().setTriangles(t).setFlags(nvrhi::rt::GeometryFlags::None);
}

class NvrhiRenderer final : public Renderer {
    struct PreviewConsts { // preview.hlsl's Params
        XMFLOAT4X4 viewProj;
        float eye[4];
        float light[4];
    };
    struct PreviewSlot { // renderPreview's target
        nvrhi::TextureHandle color, depth, atlas;
        nvrhi::FramebufferHandle fb;
        nvrhi::BindingSetHandle set;
        nvrhi::BufferHandle vb;
        TextureHandle id = 0;
        int w = 0, h = 0, aw = 0, ah = 0;
        bool pending = false;
        PreviewRequest req;
    };

public:
    template <class Window> bool init(Window window, int w, int h, bool debug) {
        if (!dev_.init(window, w, h, debug)) return false;
        if (!ui_.init(dev_.device())) return false;
        nvrhi::IDevice* d = dev_.device();
        cl_ = d->createCommandList();

        nvrhi::BufferDesc cb;
        cb.byteSize = sizeof(FrameConstants);
        cb.isConstantBuffer = true;
        cb.isVolatile = true;
        cb.maxVersions = 64;
        cb.debugName = "frame constants";
        frameCb_ = d->createBuffer(cb);
        nvrhi::BufferDesc lb;
        lb.byteSize = sizeof(LensConsts);
        lb.isConstantBuffer = true;
        lb.isVolatile = true;
        lb.maxVersions = 16;
        lb.debugName = "lens rings";
        lensCb_ = d->createBuffer(lb);

        pointClamp_ = d->createSampler(nvrhi::SamplerDesc()
                                           .setAllFilters(false)
                                           .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        // the output pass's distortions (sub-pixel shifts) read bilinearly, or they turn edges into stairs
        linearClamp_ = d->createSampler(nvrhi::SamplerDesc()
                                            .setAllFilters(true)
                                            .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        // a texture pack's pages (trilinear, anisotropic); without a pack, pointClamp_: the PS1's texels
        anisoClamp_ = d->createSampler(nvrhi::SamplerDesc()
                                           .setAllFilters(true)
                                           .setMaxAnisotropy(16.0f)
                                           .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        // the ray-query scene shader is SM 6.5
        rt_ = d->queryFeatureSupport(nvrhi::Feature::RayQuery) && dev_.rayQueryShaders();
        SDL_Log("render: ray queries %s", rt_ ? "supported (sun shadows available)" : "unsupported");
        sceneVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_scene_vs, sizeof(g_scene_vs));
        scenePs_ = rt_ ? d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_scene_rt_ps, sizeof(g_scene_rt_ps))
                       : d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_scene_ps, sizeof(g_scene_ps));
        outVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_output_vs, sizeof(g_output_vs));
        outPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_output_ps, sizeof(g_output_ps));
        heightVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_height"}, g_height_vs, sizeof(g_height_vs));
        heightPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_height"}, g_height_ps, sizeof(g_height_ps));
        flakeVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_flake"}, g_flake_vs, sizeof(g_flake_vs));
        flakePs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_flake"}, g_flake_ps, sizeof(g_flake_ps));
        fullVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_full"}, g_full_vs, sizeof(g_full_vs));
        blizzardPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_blizzard"}, g_blizzard_ps, sizeof(g_blizzard_ps));
        {
            nvrhi::BindingLayoutDesc pl; // the flakes, the blown snow: the frame, the height map, the depth copy
            pl.visibility = nvrhi::ShaderType::All;
            pl.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(4),
                           nvrhi::BindingLayoutItem::Texture_SRV(5)};
            precipLayout_ = d->createBindingLayout(pl);
        }

        auto attr = [](const char* name, nvrhi::Format f, uint32_t offset) {
            return nvrhi::VertexAttributeDesc().setName(name).setFormat(f).setOffset(offset).setElementStride(
                sizeof(GpuVertex));
        };
        const nvrhi::VertexAttributeDesc attrs[] = {
            attr("POSITION", nvrhi::Format::RGB32_FLOAT, 0), attr("UV", nvrhi::Format::RG32_FLOAT, 12),
            attr("SLICE", nvrhi::Format::R32_FLOAT, 20),     attr("SHADE", nvrhi::Format::R32_FLOAT, 24),
            attr("CULL", nvrhi::Format::R32_FLOAT, 28),      attr("LAYER", nvrhi::Format::R32_FLOAT, 32),
            attr("COLOR", nvrhi::Format::RGB32_FLOAT, 36),   attr("NORMAL", nvrhi::Format::RGB32_FLOAT, offsetof(GpuVertex, normal)),
            attr("PREVPOS", nvrhi::Format::RGB32_FLOAT, offsetof(GpuVertex, prevPos)),
            attr("FACEN", nvrhi::Format::RGB32_FLOAT, offsetof(GpuVertex, faceN)),
            attr("PAIRN", nvrhi::Format::RGB32_FLOAT, offsetof(GpuVertex, pairN)),
            attr("SURFACE", nvrhi::Format::R32_FLOAT, offsetof(GpuVertex, surface)),
            attr("UVRECT", nvrhi::Format::RGBA32_FLOAT, offsetof(GpuVertex, uvRect)),
            attr("OBJECT", nvrhi::Format::R32_FLOAT, offsetof(GpuVertex, object)),
            attr("PIECEN", nvrhi::Format::RGB32_FLOAT, offsetof(GpuVertex, pieceN))};
        inputLayout_ = d->createInputLayout(attrs, uint32_t(std::size(attrs)), sceneVs_);

        nvrhi::BindingLayoutDesc bl;
        bl.visibility = nvrhi::ShaderType::All;
        bl.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(0),
                       nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1),
                       nvrhi::BindingLayoutItem::Texture_SRV(5), nvrhi::BindingLayoutItem::Texture_SRV(6)};
        if (rt_) {
            bl.bindings.push_back(nvrhi::BindingLayoutItem::RayTracingAccelStruct(1));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(7)); // the traced light, its prepass depth
            bl.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(8));
            tlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc().setTopLevelMaxInstances(2).setDebugName("scene tlas"));
            const std::vector<float> none(4, 0.0f);
            dynAttr_ = attrBuffer(none, "dynamic rt attributes");
            uploadBuffer(dynAttr_, none);
            // the traced light (scene.hlsl lightTex): the prepass, the rays, the two blur passes
            prePs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_scene_pre_ps, sizeof(g_scene_pre_ps));
            lightPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_light"}, g_light_ps, sizeof(g_light_ps));
            lightBlurPs_[0] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_lightBlurX"}, g_light_blur_x_ps,
                                              sizeof(g_light_blur_x_ps));
            lightBlurPs_[1] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_lightBlurY"}, g_light_blur_y_ps,
                                              sizeof(g_light_blur_y_ps));
            nvrhi::BindingLayoutDesc ll;
            ll.visibility = nvrhi::ShaderType::All;
            ll.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0)};
            preLayout_ = d->createBindingLayout(ll);
            ll.bindings.push_back(nvrhi::BindingLayoutItem::RayTracingAccelStruct(1));
            ll.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
            ll.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
            ll.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(8));
            ll.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(9));
            lightLayout_ = d->createBindingLayout(ll);
            ll.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(8),
                           nvrhi::BindingLayoutItem::Texture_SRV(9), nvrhi::BindingLayoutItem::Texture_SRV(10)};
            lightBlurLayout_ = d->createBindingLayout(ll);
            // what the scene binds while no light is traced (never read: scene.hlsl lightTerms)
            lightNone_ = makeTarget(1, 1, nvrhi::Format::RG8_UNORM, "no traced light");
            preDepthNone_ = makeTarget(1, 1, nvrhi::Format::R32_FLOAT, "no light prepass");
        }
        sceneLayout_ = d->createBindingLayout(bl);
        outlinePs_[0] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_outline"}, g_outline_ps, sizeof(g_outline_ps));
        outlinePs_[1] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_outline"}, g_outline_ms_ps, sizeof(g_outline_ms_ps));
        {
            nvrhi::BindingLayoutDesc ol; // the frame, the depth copy, the normal target
            ol.visibility = nvrhi::ShaderType::All;
            ol.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(5),
                           nvrhi::BindingLayoutItem::Texture_SRV(6), nvrhi::BindingLayoutItem::Texture_SRV(11)};
            outlineLayout_ = d->createBindingLayout(ol);
        }
        bloomPs_[0] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_down"}, g_bloom_down_ps, sizeof(g_bloom_down_ps));
        bloomPs_[1] = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_up"}, g_bloom_up_ps, sizeof(g_bloom_up_ps));
        {
            nvrhi::BindingLayoutDesc ml;
            ml.visibility = nvrhi::ShaderType::Pixel;
            ml.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(BloomConstants)),
                           nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
            bloomLayout_ = d->createBindingLayout(ml);
        }
        nvrhi::BindingLayoutDesc ol;
        ol.visibility = nvrhi::ShaderType::Pixel;
        ol.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(OutConsts)), nvrhi::BindingLayoutItem::Texture_SRV(0),
                       nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1),
                       nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::VolatileConstantBuffer(1)};
        outLayout_ = d->createBindingLayout(ol);
        nvrhi::BindingLayoutDesc dl;
        dl.visibility = nvrhi::ShaderType::Pixel;
        dl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(int) * 4),
                       nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
        debugLayout_ = d->createBindingLayout(dl);
        debugVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_debugview_vs, sizeof(g_debugview_vs));
        debugPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_debugview_ps, sizeof(g_debugview_ps));
        debugMsPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_debugview_ms_ps,
                                     sizeof(g_debugview_ms_ps));
        previewVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_preview_vs, sizeof(g_preview_vs));
        previewPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_preview_ps, sizeof(g_preview_ps));
        {
            auto pa = [](const char* name, nvrhi::Format f, uint32_t offset) {
                return nvrhi::VertexAttributeDesc().setName(name).setFormat(f).setOffset(offset).setElementStride(
                    sizeof(PreviewVertex));
            };
            const nvrhi::VertexAttributeDesc pattrs[] = {pa("POSITION", nvrhi::Format::RGB32_FLOAT, 0),
                                                         pa("UV", nvrhi::Format::RG32_FLOAT, 12),
                                                         pa("COLOR", nvrhi::Format::RGB32_FLOAT, 20)};
            previewInput_ = d->createInputLayout(pattrs, uint32_t(std::size(pattrs)), previewVs_);
            nvrhi::BindingLayoutDesc pl;
            pl.visibility = nvrhi::ShaderType::All;
            pl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PreviewConsts)),
                           nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
            previewLayout_ = d->createBindingLayout(pl);
        }

        createTargets(dev_.width(), dev_.height());
        buildHeightMap({}); // no room yet: no surfaces
        if (!opaque_ || !sky_pipe_ || !semi_[0] || !semi_[1] || !semi_[2] || !semi_[3] || !flakePipe_ || !blizzardPipe_ ||
            !height_) {
            SDL_Log("render: the scene pipelines failed to create");
            return false;
        }
        setAntiAliasing(AaBackend::None, AaQuality::NativeAA);
        return cl_ && frameCb_ && sceneVs_ && scenePs_ && outVs_ && outPs_ && inputLayout_;
    }

    ~NvrhiRenderer() override {
        if (dev_.device()) dev_.device()->waitForIdle();
    }

    bool upload(const std::vector<RenderVertex>& tris, int sliceCount, const std::array<uint32_t, 4>& semiStart,
                int sliceSize) override {
        sliceSize_ = sliceSize;
        sliceUpdates_.clear();
        poolShows_.clear(); // the last room's slices (the pool itself stays)
        sliceWants_.clear();
        staticGv_.clear();
        uvDirtyLo_ = UINT32_MAX;
        uvDirtyHi_ = 0;
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        vertexCount_ = uint32_t(tris.size());
        semiStart_ = semiStart;
        if (semiStart_[3] == 0) semiStart_.fill(vertexCount_); // all opaque
        vbuf_ = nullptr;
        pages_ = nullptr;
        sceneSet_ = nullptr;
        staticBlas_ = nullptr;
        staticAttr_ = nullptr;
        if (tris.empty() || sliceCount <= 0) {
            buildHeightMap({}); // the last room's surfaces are gone
            return bool(height_);
        }

        // the room's normals smoothed as a model's (its creases kept): on its few large faces the sun's light varies
        // across a slope instead of stepping from face to face
        std::vector<GpuVertex> gv;
        const std::vector<std::pair<size_t, size_t>> wholeRoom{{0, tris.size()}};
        toGpu(tris, nullptr, gv, &wholeRoom);
        nvrhi::BufferDesc bd;
        bd.byteSize = gv.size() * sizeof(GpuVertex);
        bd.isVertexBuffer = true;
        bd.isAccelStructBuildInput = rt_;
        bd.initialState = nvrhi::ResourceStates::VertexBuffer;
        bd.keepInitialState = true;
        bd.debugName = "room vertices";
        vbuf_ = d->createBuffer(bd);

        nvrhi::TextureDesc td;
        td.width = td.height = uint32_t(sliceSize);
        td.arraySize = uint32_t(sliceCount + kSpareSlices);
        td.mipLevels = pageMips(sliceSize);
        td.dimension = nvrhi::TextureDimension::Texture2DArray;
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::ShaderResource;
        td.keepInitialState = true;
        td.debugName = "texture pages";
        pages_ = d->createTexture(td);
        if (!vbuf_ || !pages_) return false;

        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeBuffer(vbuf_, gv.data(), bd.byteSize);
        if (rt_) { // shadow casters: the opaque range (semi-transparent water / glass lets the sun through)
            std::vector<float> attr;
            toAttr(gv, semiStart_[0], attr);
            staticAttr_ = attrBuffer(attr, "room rt attributes");
            cl->writeBuffer(staticAttr_, attr.data(), attr.size() * sizeof(float));
            const uint32_t casters = semiStart_[0] / 3 * 3;
            if (casters) {
                const nvrhi::rt::GeometryDesc g = triangleGeometry(vbuf_, sequentialIndices(cl, casters), casters);
                staticBlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                                       .addBottomLevelGeometry(g)
                                                       .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
                                                       .setDebugName("room blas"));
                cl->buildBottomLevelAccelStruct(staticBlas_, &g, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
            }
        }
        cl->close();
        d->executeCommandList(cl);
        d->waitForIdle(); // the BLAS build scratch memory lives in cl: keep it until the GPU is done
        staticGv_ = std::move(gv); // CPU copy for updateStaticUvs
        makeSceneSet();
        buildHeightMap(tris);
        return bool(sceneSet_) && bool(height_);
    }

    void setSun(const SunLight& sun) override { sun_ = sun; }
    void setAmbientOcclusion(const AmbientOcclusion& ao) override { occlusion_ = ao; }
    void setBloom(const Bloom& bloom) override { bloom_ = bloom; }
    void setToonStyle(const ToonStyle& style) override { toon_ = style; }
    void setParticleLighting(const ParticleLighting& l) override { particleLighting_ = l; }
    void setModelSubdivision(const ModelSubdivision& s) override { subdivision_ = s; }
    void setPrecipitation(const Precipitation& p) override { precip_ = p; }
    void setSeeThrough(const std::vector<SeeThroughBody>& bodies, const std::vector<SeeThroughPiece>& pieces,
                       bool debug) override {
        seeBodies_.assign(bodies.begin(), bodies.begin() + std::ptrdiff_t(std::min(bodies.size(), kMaxSeeThroughBodies)));
        seePieces_.assign(pieces.begin(), pieces.begin() + std::ptrdiff_t(std::min(pieces.size(), kMaxSeeThroughPieces)));
        seeDebug_ = debug;
    }
    bool rayTracingSupported() const override { return rt_; }

    void updateSlice(int slice, const std::vector<uint8_t>& rgba) override {
        if (pages_ && slice >= 0 && uint32_t(slice) < pages_->getDesc().arraySize &&
            rgba.size() == size_t(sliceSize_) * size_t(sliceSize_) * 4) {
            sliceUpdates_[slice] = rgba;
            poolShows_.erase(slice);
            sliceWants_.erase(slice);
        }
    }

    bool showPage(int slice, uint64_t page) override {
        if (!pages_ || slice < 0 || uint32_t(slice) >= pages_->getDesc().arraySize) return false;
        sliceWants_[slice] = page;
        const auto it = poolEdge_ == sliceSize_ ? poolLayer_.find(page) : poolLayer_.end();
        if (it == poolLayer_.end()) return false;
        poolOrder_.splice(poolOrder_.begin(), poolOrder_, it->second.second);
        poolShows_[slice] = it->second.first;
        sliceUpdates_.erase(slice);
        return true;
    }

    bool poolPage(uint64_t page, std::vector<uint8_t>& chain) override {
        const size_t bytes = size_t(sliceSize_) * size_t(sliceSize_) * 4;
        if (chain.size() < bytes) return false;
        if (poolEdge_ != sliceSize_) { // another pack scale: the pool starts over
            pool_ = nullptr;
            poolEdge_ = sliceSize_;
            poolUsed_ = 0;
            poolOrder_.clear();
            poolLayer_.clear();
            poolWrites_.clear();
            poolShows_.clear();
        }
        if (poolLayer_.contains(page)) return true;
        const uint32_t most = uint32_t(std::clamp<size_t>(kPoolBytes / bytes, 1, 2048));
        uint32_t layer = 0;
        if (pool_ && poolUsed_ < pool_->getDesc().arraySize) {
            layer = poolUsed_++;
        } else if (!pool_ || pool_->getDesc().arraySize < most) {
            if (!growPool(pool_ ? std::min(most, pool_->getDesc().arraySize * 2) : std::min(most, kPoolFirstLayers))) return false;
            layer = poolUsed_++;
        } else { // full: the least recently shown page no queued copy reads
            auto old = poolOrder_.end();
            bool found = false;
            while (!found && old != poolOrder_.begin()) {
                --old;
                const uint32_t l = poolLayer_[*old].first;
                found = std::none_of(poolShows_.begin(), poolShows_.end(), [&](const auto& s) { return s.second == l; });
            }
            if (!found) return false; // every layer is read this frame
            layer = poolLayer_[*old].first;
            poolLayer_.erase(*old);
            poolOrder_.erase(old);
        }
        poolOrder_.push_front(page);
        poolLayer_[page] = {layer, poolOrder_.begin()};
        poolWrites_[layer] = std::move(chain);
        for (const auto& [slice, want] : sliceWants_)
            if (want == page) poolShows_[slice] = layer;
        return true;
    }

    static uint32_t pageMips(int edge) { return pageMipLevels(edge); }
    // a sliceSize_^2 RGBA8 page and its smaller levels, one after the other (appendPageMips)
    void writeChain(nvrhi::ICommandList* cl, nvrhi::ITexture* tex, uint32_t layer, const uint8_t* chain) {
        for (uint32_t level = 0, edge = uint32_t(sliceSize_); level < pageMips(sliceSize_); ++level, edge /= 2) {
            cl->writeTexture(tex, layer, level, chain, size_t(edge) * 4);
            chain += size_t(edge) * edge * 4;
        }
    }
    void writePage(nvrhi::ICommandList* cl, nvrhi::ITexture* tex, uint32_t layer, const uint8_t* rgba) {
        std::vector<uint8_t> chain(rgba, rgba + size_t(sliceSize_) * size_t(sliceSize_) * 4);
        appendPageMips(chain, sliceSize_);
        writeChain(cl, tex, layer, chain.data());
    }
    static void copyPage(nvrhi::ICommandList* cl, nvrhi::ITexture* to, uint32_t toLayer, nvrhi::ITexture* from, uint32_t fromLayer) {
        for (uint32_t level = 0; level < from->getDesc().mipLevels; ++level)
            cl->copyTexture(to, nvrhi::TextureSlice().setArraySlice(toLayer).setMipLevel(level), from,
                            nvrhi::TextureSlice().setArraySlice(fromLayer).setMipLevel(level));
    }

    // pool_ with `layers` layers, the pooled ones copied (their pending writes still apply)
    bool growPool(uint32_t layers) {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::TextureDesc td;
        td.width = td.height = uint32_t(poolEdge_);
        td.arraySize = layers;
        td.mipLevels = pageMips(poolEdge_);
        td.dimension = nvrhi::TextureDimension::Texture2DArray;
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::CopySource;
        td.keepInitialState = true;
        td.debugName = "page pool";
        nvrhi::TextureHandle grown = d->createTexture(td);
        if (!grown) return false;
        if (pool_) {
            d->waitForIdle();
            nvrhi::CommandListHandle cl = d->createCommandList();
            cl->open();
            for (uint32_t i = 0; i < poolUsed_; ++i) copyPage(cl, grown, i, pool_, i);
            cl->close();
            d->executeCommandList(cl);
            d->waitForIdle();
        }
        pool_ = grown;
        return true;
    }

    bool appendSlices(const std::vector<uint8_t>& rgba, int count) override {
        const size_t sliceBytes = size_t(sliceSize_) * size_t(sliceSize_) * 4;
        if (!pages_ || count <= 0 || rgba.size() != size_t(count) * sliceBytes) return false;
        return growSlices(count, rgba.data());
    }

    bool reserveSlices(int count) override { return pages_ && count > 0 && growSlices(count, nullptr); }
    int sliceTotal() const override { return pages_ ? int(pages_->getDesc().arraySize) : 0; }

    // pages_ with `count` more slices (written from rgba, if any), the ones so far copied
    bool growSlices(int count, const uint8_t* rgba) {
        const size_t sliceBytes = size_t(sliceSize_) * size_t(sliceSize_) * 4;
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        nvrhi::TextureDesc td = pages_->getDesc();
        const uint32_t old = td.arraySize;
        td.arraySize = old + uint32_t(count);
        nvrhi::TextureHandle grown = d->createTexture(td);
        if (!grown) return false;
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        for (uint32_t i = 0; i < old; ++i) // the slices so far (their pending updateSlice writes still apply)
            copyPage(cl, grown, i, pages_, i);
        for (int k = 0; rgba && k < count; ++k) writePage(cl, grown, old + uint32_t(k), rgba + size_t(k) * sliceBytes);
        cl->close();
        d->executeCommandList(cl);
        d->waitForIdle();
        pages_ = grown;
        makeSceneSet();
        return bool(sceneSet_);
    }

    void updateStaticUvs(const std::vector<StaticUvPatch>& patches) override {
        for (const StaticUvPatch& p : patches) {
            if (p.vertex >= staticGv_.size()) continue;
            staticGv_[p.vertex].v.uv[0] = p.uv[0];
            staticGv_[p.vertex].v.uv[1] = p.uv[1];
            uvDirtyLo_ = std::min(uvDirtyLo_, p.vertex);
            uvDirtyHi_ = std::max(uvDirtyHi_, p.vertex + 1);
            if (const uint32_t first = p.vertex / 3 * 3; first + 3 <= staticGv_.size()) {
                setUvRect(&staticGv_[first]);
                uvDirtyLo_ = std::min(uvDirtyLo_, first);
                uvDirtyHi_ = std::max(uvDirtyHi_, first + 3);
            }
        }
    }
    // the dirty vertex range of updateStaticUvs into the room vertex buffer (+ the shadow rays' alpha-test UVs)
    void flushStaticUvs() {
        if (uvDirtyLo_ >= uvDirtyHi_ || !vbuf_) return;
        const uint32_t lo = uvDirtyLo_, hi = std::min<uint32_t>(uvDirtyHi_, uint32_t(staticGv_.size()));
        uvDirtyLo_ = UINT32_MAX;
        uvDirtyHi_ = 0;
        if (lo >= hi) return;
        cl_->writeBuffer(vbuf_, &staticGv_[lo], size_t(hi - lo) * sizeof(GpuVertex), uint64_t(lo) * sizeof(GpuVertex));
        const uint32_t casters = std::min(hi, semiStart_[0]);
        if (staticAttr_ && lo < casters) { // toAttr's layout: float4 (u, v, slice, 0) per opaque vertex
            std::vector<float> attr(size_t(casters - lo) * 4, 0.0f);
            for (uint32_t i = lo; i < casters; ++i) {
                attr[size_t(i - lo) * 4 + 0] = staticGv_[i].v.uv[0];
                attr[size_t(i - lo) * 4 + 1] = staticGv_[i].v.uv[1];
                attr[size_t(i - lo) * 4 + 2] = staticGv_[i].v.slice;
            }
            cl_->writeBuffer(staticAttr_, attr.data(), attr.size() * sizeof(float), uint64_t(lo) * 4 * sizeof(float));
        }
    }

    void setClearColor(float r, float g, float b) override {
        clear_[0] = r;
        clear_[1] = g;
        clear_[2] = b;
    }

    void setFog(const Fog& fog) override { fog_ = fog; }
    void setEffectNearFade(float from, float to) override {
        effectNearFade_[0] = from;
        effectNearFade_[1] = to;
    }
    void setScreenRipples(const std::vector<ScreenRipple>& ripples) override { ripples_ = ripples; }
    void setLensRings(const std::vector<LensRing>& rings) override { lensRings_ = rings; }
    void setScreenTints(const std::vector<ScreenTint>& tints) override { tints_ = tints; }
    void setScreenWave(float amplitude, float phase, const float tint[3], bool remaster, float seconds, bool heat) override {
        waveHeat_ = heat;
        wave_[0] = amplitude;
        wave_[1] = phase;
        waveRemaster_ = remaster;
        waveSeconds_ = seconds;
        for (int k = 0; k < 3; ++k) waveTint_[k] = tint[k];
    }

    void uploadSky(const std::vector<RenderVertex>& tris, bool screenSpace, bool isSky, const SkyPoles& poles) override {
        skyScreen_ = screenSpace;
        skyIsSky_ = isSky;
        skyPoles_ = poles;
        sky_ = tris; // GPU copy made in draw(): the dome's previous positions need the camera
    }

    FrameCosts frameCosts() const override { return costs_; }

    void uploadDynamic(const std::vector<DynamicObject>& objects) override {
        const auto began = std::chrono::steady_clock::now();
        const auto isModel = [](uint32_t kind) {
            return kind == kKindActor || kind == kKindPlayer || kind == kKindPoolModel || kind == kKindWalker;
        };
        // opaque objects first, then the blended ones by PS1 mode; dynStart_[m] = vertices with blend <= m
        std::vector<DynamicObject> sorted = objects;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const DynamicObject& a, const DynamicObject& b) { return a.blend < b.blend; });
        dyn_.clear();
        dynStart_.fill(0);
        std::vector<std::pair<size_t, size_t>> models; // the objects' vertex ranges, smoothed per object
        std::vector<std::pair<size_t, size_t>> pieces; // ... the models' of the anime look, shaded by their pieces
        // the surface bits by object: the model draws (exe 0x5f4980 / 0x5f3820) and the effects
        std::vector<std::pair<size_t, size_t>> surfaces;
        std::vector<uint32_t> surfaceBits;
        for (const DynamicObject& o : sorted) {
            const int b = std::min<int>(o.blend, 4);
            for (int m = b; m < 5; ++m) dynStart_[m] += uint32_t(o.tris.size());
            const uint32_t kind = o.id >> 24;
            // effects: particles / rings / billboards keep their face normals; a subdivided model has its plan's
            if (kind != kKindEffect && !(subdivision_.level > 1 && isModel(kind)))
                models.push_back({dyn_.size(), dyn_.size() + o.tris.size()});
            uint32_t bits = 0;
            if (isModel(kind)) bits |= kSurfaceModel;
            if (isModel(kind) && toon_.shading) pieces.push_back({dyn_.size(), dyn_.size() + o.tris.size()});
            if (kind == kKindEffect) bits |= kSurfaceEffect;
            if (o.nearFade) bits |= kSurfaceNearFade;
            if (o.glow[0] > 0.0f || o.glow[1] > 0.0f || o.glow[2] > 0.0f) bits |= kSurfaceGlow;
            if (bits) {
                surfaces.push_back({dyn_.size(), dyn_.size() + o.tris.size()});
                surfaceBits.push_back(bits);
            }
            dyn_.insert(dyn_.end(), o.tris.begin(), o.tris.end());
        }
        particleLights_.clear();
        if (particleLighting_.enabled)
            particleLights_ = gatherParticleLights(sorted, particleLighting_.radius, kMaxParticleLights);
        history_.apply(sorted, dynPrev_);
        toGpu(dyn_, dynPrev_.data(), dynGpu_, &models, &surfaces, &surfaceBits, &pieces);
        {
            size_t at = 0, number = 0;
            for (const DynamicObject& o : sorted) {
                ++number;
                for (size_t k = 0; k < o.tris.size(); ++k) dynGpu_[at + k].object = float(number);
                at += o.tris.size();
            }
        }
        // the subdivision: here only what the GPU needs to make it (expandDynamic): the models' vertices take their
        // plan's normals over this frame's faces, and each triangle says where its vertices go and how it splits
        dynTris_.clear();
        dynCount_ = uint32_t(dynGpu_.size());
        if (subdivision_.level > 1 && !dynGpu_.empty()) {
            const uint32_t split = uint32_t(subdivision_.level * subdivision_.level) * 3;
            dynStart_.fill(0);
            size_t at = 0;
            uint32_t out = 0;
            for (const DynamicObject& o : sorted) {
                const uint32_t first = out;
                const size_t count = o.tris.size();
                GpuVertex* v = dynGpu_.data() + at;
                const SubdivisionPlan* plan = isModel(o.id >> 24) ? subdivisionPlan(o, dyn_.data() + at) : nullptr;
                if (plan) {
                    planFaces_.resize(count);
                    for (size_t f = 0; f + 2 < count; f += 3) std::memcpy(&planFaces_[f], v[f].faceN, 3 * sizeof(float));
                    for (size_t k = 0; k < count; ++k) subdivisionNormal(*plan, k, planFaces_.data(), v[k].normal);
                }
                for (size_t f = 0; f + 2 < count; f += 3) {
                    uint32_t flags = 0;
                    if (plan) {
                        const GpuVertex& a = v[f];
                        const bool opposite = a.faceN[0] * a.pairN[0] + a.faceN[1] * a.pairN[1] + a.faceN[2] * a.pairN[2] < 0.0f;
                        const SubdividedCull cull = subdividedCull(int(std::lround(a.v.cull)), opposite);
                        flags = kTriSplit | uint32_t(plan->roundEdges[f / 3]) << kTriRoundShift | (cull.mode ? kTriCullOne : 0) |
                                (cull.reverse ? kTriReverse : 0);
                    }
                    dynTris_.insert(dynTris_.end(), {uint32_t(at + f), out, flags});
                    out += plan ? split : 3;
                }
                for (int m = std::min<int>(o.blend, 4); m < 5; ++m) dynStart_[m] += out - first;
                at += count;
            }
            dynCount_ = out;
        }
        costs_.dynamicMs = msSince(began);
        costs_.dynamicVertices = dynCount_;
        costs_.dynamicOpaque = dynStart_[0];
    }

    // the plan of a model, made once per id, vertex count and layout; none for a pose fallen on one point
    const SubdivisionPlan* subdivisionPlan(const DynamicObject& o, const RenderVertex* tris) {
        ModelPlan& entry = modelPlans_[o.id];
        if (entry.count != o.tris.size() || entry.layout != o.layout || entry.plan.roundEdges.empty()) {
            entry.count = o.tris.size();
            entry.layout = o.layout;
            if (!buildSubdivisionPlan(tris, o.tris.size(), entry.plan)) return nullptr;
        }
        return &entry.plan;
    }

    void resetHistory() override {
        modelPlans_.clear();
        history_.reset();
        historyValid_ = false;
        pendingReset_ = true;
    }

    void resize(int w, int h) override {
        if (w <= 0 || h <= 0) return; // minimised
        if (w == dev_.width() && h == dev_.height()) return;
        dev_.device()->waitForIdle();
        backFbs_.clear(); // framebuffers hold the swapchain images: ResizeBuffers fails while they live
        dev_.resize(w, h);
        broken_.clear(); // run-time failures can be size-specific (DLSS feature creation at 1x1)
        setAntiAliasing(wanted_, quality_); // recreates the upscaler and the targets at the new size
    }

    void setVsync(bool on) override { vsync_ = on; }

    void setAntiAliasing(AaBackend backend, AaQuality quality) override {
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        wanted_ = backend;
        quality_ = quality;
        up_.reset();
        for (AaBackend b : backendCandidates(backend, broken_)) {
            std::unique_ptr<Upscaler> u = createUpscaler(b);
            if (u && u->init(d, dev_.width(), dev_.height(), quality)) {
                up_ = std::move(u);
                break;
            }
        }
        if (up_->backend() != backend && backend != AaBackend::Auto)
            SDL_Log("aa: %s unavailable, falling back to auto", aaBackendName(backend));
        int rw = dev_.width(), rh = dev_.height();
        up_->renderSize(dev_.width(), dev_.height(), quality, rw, rh);
        createTargets(rw, rh);
        frameIndex_ = 0;
        historyValid_ = false;
        pendingReset_ = true;
        SDL_Log("aa: %s %s render %dx%d -> output %dx%d", aaBackendName(up_->backend()), aaQualityName(quality), rw, rh,
                dev_.width(), dev_.height());
    }

    std::vector<AaBackend> availableBackends() override {
        if (available_.empty()) {
            dev_.device()->waitForIdle();
            for (AaBackend b : {AaBackend::Dlss, AaBackend::Fsr, AaBackend::Xess, AaBackend::Nis, AaBackend::Msaa,
                                AaBackend::Ssaa, AaBackend::None}) {
                std::unique_ptr<Upscaler> u = createUpscaler(b);
                if (u && u->init(dev_.device(), dev_.width(), dev_.height(), AaQuality::NativeAA)) available_.push_back(b);
            }
        }
        return available_;
    }

    AaBackend activeBackend() const override { return up_ ? up_->backend() : AaBackend::None; }

    void imguiInit() override { imgui_.init(&ui_); }
    void imguiNewFrame() override {}
    void imguiShutdown() override {}
    void renderImGui(ImDrawData* data) override {
        if (backFb_) imgui_.render(cl_, backFb_, data);
    }
    void renderCanvas(const ui::Canvas& c) override {
        if (backFb_)
            ui_.draw(cl_, backFb_, UiPass::Game, c.vertices().data(), c.vertices().size(), c.indices().data(),
                     c.indices().size(), c.cmds().data(), c.cmds().size(), {0.0f, 0.0f}, {c.width(), c.height()});
    }

    TextureHandle createTexture(int w, int h, const uint32_t* rgba) override {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::TextureDesc td;
        td.width = uint32_t(w);
        td.height = uint32_t(h);
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::ShaderResource;
        td.keepInitialState = true;
        td.debugName = "ui texture";
        nvrhi::TextureHandle tex = d->createTexture(td);
        if (!tex) return 0;
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeTexture(tex, 0, 0, rgba, size_t(w) * 4);
        cl->close();
        d->executeCommandList(cl);
        return ui_.addTexture(tex);
    }
    void updateTexture(TextureHandle tex, const uint32_t* rgba) override {
        nvrhi::ITexture* t = ui_.texture(tex);
        if (!t) return;
        nvrhi::IDevice* d = dev_.device();
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeTexture(t, 0, 0, rgba, size_t(t->getDesc().width) * 4);
        cl->close();
        d->executeCommandList(cl);
    }
    void destroyTexture(TextureHandle tex) override {
        dev_.device()->waitForIdle();
        ui_.removeTexture(tex);
    }

    TextureHandle sceneGrab(int x, int y, int w, int h) override {
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
        if (!grabTex_ || grabW_ != w || grabH_ != h) {
            nvrhi::IDevice* d = dev_.device();
            if (grabId_) {
                d->waitForIdle();
                ui_.removeTexture(grabId_);
                grabId_ = 0;
            }
            nvrhi::TextureDesc td;
            td.width = uint32_t(w);
            td.height = uint32_t(h);
            td.format = dev_.backbuffer()->getDesc().format; // the back buffer's (copied, then sampled)
            td.initialState = nvrhi::ResourceStates::ShaderResource;
            td.keepInitialState = true;
            td.debugName = "scene grab";
            grabTex_ = d->createTexture(td);
            if (!grabTex_) return 0;
            grabW_ = w;
            grabH_ = h;
            grabId_ = ui_.addTexture(grabTex_);
        }
        grabX_ = x;
        grabY_ = y;
        grabPending_ = true;
        return grabId_;
    }

    TextureHandle renderPreview(int slot, int w, int h, const PreviewRequest& req) override {
        if (slot < 0 || slot >= int(std::size(preview_)) || w <= 0 || h <= 0 || w > 2048 || h > 2048) return 0;
        if (req.tris.empty() || req.atlasW <= 0 || req.atlasH <= 0 ||
            req.atlas.size() < size_t(req.atlasW) * size_t(req.atlasH) || !previewLayout_)
            return 0;
        PreviewSlot& ps = preview_[slot];
        nvrhi::IDevice* d = dev_.device();
        if (!ps.color || ps.w != w || ps.h != h) {
            d->waitForIdle();
            if (ps.id) ui_.removeTexture(ps.id);
            ps.color = makeTarget(w, h, nvrhi::Format::RGBA8_UNORM, "preview colour");
            ps.depth = makeTarget(w, h, kDepthFormat, "preview depth", true);
            ps.fb = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(ps.color).setDepthAttachment(ps.depth));
            ps.id = ui_.addTexture(ps.color);
            ps.w = w;
            ps.h = h;
        }
        if (!ps.atlas || ps.aw != req.atlasW || ps.ah != req.atlasH) {
            nvrhi::TextureDesc td;
            td.width = uint32_t(req.atlasW);
            td.height = uint32_t(req.atlasH);
            td.format = nvrhi::Format::RGBA8_UNORM;
            td.initialState = nvrhi::ResourceStates::ShaderResource;
            td.keepInitialState = true;
            td.debugName = "preview atlas";
            ps.atlas = d->createTexture(td);
            ps.aw = req.atlasW;
            ps.ah = req.atlasH;
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(PreviewConsts)),
                           nvrhi::BindingSetItem::Texture_SRV(0, ps.atlas), nvrhi::BindingSetItem::Sampler(0, pointClamp_)};
            ps.set = d->createBindingSet(bs, previewLayout_);
        }
        if (!previewPipe_) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.inputLayout = previewInput_;
            pd.VS = previewVs_;
            pd.PS = previewPs_;
            pd.bindingLayouts = {previewLayout_};
            pd.renderState.rasterState.setCullNone(); // the models' faces are wound either way
            pd.renderState.depthStencilState.depthTestEnable = true;
            pd.renderState.depthStencilState.depthWriteEnable = true;
            pd.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
            previewPipe_ = d->createGraphicsPipeline(pd, ps.fb->getFramebufferInfo());
        }
        ps.req = req;
        ps.pending = true;
        return ps.id;
    }

    void setDebugView(DebugView view) override { view_ = view; }

    bool motionSelfTest(const Camera& a, const Camera& b, float& maxErrPx, float& staticMaxPx) override {
        nvrhi::IDevice* d = dev_.device();
        if (!vbuf_ || !sceneSet_) return false;
        const std::vector<GpuVertex> dynKeep = dynGpu_;
        const uint32_t dynCountKeep = dynCount_;
        const std::vector<RenderVertex> skyKeep = sky_;
        dynGpu_.clear(); // static scene only: actors would move between the two frames
        dynCount_ = 0;
        jitter_ = {0.0f, 0.0f};
        sky_.clear();
        auto frame = [&](const Camera& cam, std::vector<float>* motion, std::vector<float>* depth) {
            cl_->open();
            clearTargets();
            drawScene(cam);
            nvrhi::StagingTextureHandle sm, sd;
            if (motion) sm = stage(t_.motion);
            if (depth) sd = stage(copyDepth());
            cl_->close();
            d->executeCommandList(cl_);
            d->waitForIdle();
            if (motion) readFloats(sm, nvrhi::Format::RG16_FLOAT, 2, *motion);
            if (depth) readFloats(sd, nvrhi::Format::R32_FLOAT, 1, *depth);
        };
        std::vector<float> mv, dep, still;
        resetHistory();
        frame(a, nullptr, nullptr);
        frame(b, &mv, &dep);
        frame(b, &still, nullptr);
        dynGpu_ = dynKeep;
        dynCount_ = dynCountKeep;
        sky_ = skyKeep;
        resetHistory();

        const XMMATRIX vpA = cameraViewProj(a), vpB = cameraViewProj(b);
        XMFLOAT4X4 fa, fb;
        XMStoreFloat4x4(&fa, vpA);
        XMStoreFloat4x4(&fb, vpB);
        const XMMATRIX inv = XMMatrixInverse(nullptr, vpB);
        const int w = t_.renderW, h = t_.renderH;
        maxErrPx = 0.0f;
        staticMaxPx = 0.0f;
        int samples = 0;
        for (int y = 4; y < h; y += 8)
            for (int x = 4; x < w; x += 8) {
                const size_t i = size_t(y) * size_t(w) + size_t(x);
                staticMaxPx = std::max({staticMaxPx, std::fabs(still[i * 2]), std::fabs(still[i * 2 + 1])});
                const float z = dep[i];
                if (z >= 1.0f) continue; // cleared: sky / background
                const float nx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f;
                const float ny = 1.0f - (float(y) + 0.5f) / float(h) * 2.0f;
                XMFLOAT4 wp;
                XMStoreFloat4(&wp, XMVector4Transform(XMVectorSet(nx, ny, z, 1.0f), inv));
                const float world[3] = {wp.x / wp.w, wp.y / wp.w, wp.z / wp.w};
                float ref[2];
                motionPixels(&fb._11, &fa._11, world, w, h, ref);
                maxErrPx = std::max({maxErrPx, std::fabs(mv[i * 2] - ref[0]), std::fabs(mv[i * 2 + 1] - ref[1])});
                ++samples;
            }
        SDL_Log("motion selftest: %d samples", samples);
        return samples > 0;
    }

    void draw(const Camera& cam, std::vector<uint8_t>* readback, const std::function<void()>& overlay) override {
        if (up_->failed()) { // run-time failure last frame: never use that backend again this session
            SDL_Log("aa: %s failed at run time, switching", aaBackendName(up_->backend()));
            broken_.insert(up_->backend());
            setAntiAliasing(wanted_, quality_);
        }
        nvrhi::IDevice* d = dev_.device();
        nvrhi::ITexture* back = dev_.backbuffer();
        const auto began = std::chrono::steady_clock::now();
        cl_->open();
        costs_.pagesWritten = int(sliceUpdates_.size() + poolWrites_.size());
        costs_.pagesShown = int(poolShows_.size());
        for (const auto& [slice, rgba] : sliceUpdates_) // animated CLUTs (updateSlice)
            writePage(cl_, pages_, uint32_t(slice), rgba.data());
        sliceUpdates_.clear();
        for (const auto& [layer, chain] : poolWrites_) // poolPage
            writeChain(cl_, pool_, layer, chain.data());
        poolWrites_.clear();
        for (const auto& [slice, layer] : poolShows_) // showPage: the animated pages' frames
            copyPage(cl_, pages_, uint32_t(slice), pool_, layer);
        poolShows_.clear();
        costs_.pagesMs = msSince(began);
        const auto recorded = std::chrono::steady_clock::now();
        flushStaticUvs(); // the room UV animation (updateStaticUvs)
        for (PreviewSlot& ps : preview_) // renderPreview: before the overlay samples them
            if (ps.pending) drawPreview(ps);
        nearZ_ = cam.nearZ;
        camPitch_ = cam.pitch;
        tanHalfFov_ = std::tan(cam.fovY * 0.5f);
        clearTargets();
        jitter_ = up_->temporal() ? jitterAt(frameIndex_++, up_->phaseCount(t_.renderW, t_.outW)) : Jitter{0.0f, 0.0f};
        if (vbuf_ && sceneSet_) drawScene(cam);
        if (t_.samples > 1) { // aa=msaa: the samples resolved (averaged) into the render-size targets
            cl_->resolveTexture(t_.color, nvrhi::AllSubresources, t_.msColor, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.motion, nvrhi::AllSubresources, t_.msMotion, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.normal, nvrhi::AllSubresources, t_.msNormal, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.reactive, nvrhi::AllSubresources, t_.msReactive, nvrhi::AllSubresources);
            if (glowDrawn_) cl_->resolveTexture(t_.glow, nvrhi::AllSubresources, t_.msGlow, nvrhi::AllSubresources);
        }
        if (glowDrawn_) drawBloom(); // under the blown snow: the medium dims it as it dims the scene
        glowDrawn_ = false;
        if (vbuf_ && sceneSet_ && precip_.blizzard > 0.0f) drawBlowingSnow();
        if (view_ == DebugView::Final) {
            upscale(cam);
            drawOutput(t_.upscaled, back);
        } else if (view_ == DebugView::Color) {
            drawOutput(t_.color, back);
        } else {
            drawDebug(back);
        }
        if (grabPending_) copyGrab(back); // sceneGrab: the picture without the overlay
        backFb_ = backFramebuffer(back);
        if (overlay) overlay(); // UI at output resolution, after the scene (and later the upscaler)
        backFb_ = nullptr;
        nvrhi::StagingTextureHandle staging;
        if (readback) {
            nvrhi::TextureDesc sd = back->getDesc();
            sd.isRenderTarget = false;
            sd.initialState = nvrhi::ResourceStates::CopyDest;
            sd.keepInitialState = true;
            staging = d->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);
            cl_->copyTexture(staging, nvrhi::TextureSlice(), back, nvrhi::TextureSlice());
        }
        cl_->close();
        costs_.recordMs = msSince(recorded);
        const auto submitted = std::chrono::steady_clock::now();
        d->executeCommandList(cl_);
        if (readback) readBack(staging, *readback);
        costs_.submitMs = msSince(submitted);
        const auto presented = std::chrono::steady_clock::now();
        dev_.present(vsync_);
        costs_.presentMs = msSince(presented);
    }

private:
    static float msSince(std::chrono::steady_clock::time_point from) {
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - from).count();
    }

    void drawPreview(PreviewSlot& ps) {
        ps.pending = false;
        const PreviewRequest& r = ps.req;
        const uint64_t bytes = r.tris.size() * sizeof(PreviewVertex);
        if (!ps.vb || ps.vb->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.isVertexBuffer = true;
            bd.initialState = nvrhi::ResourceStates::VertexBuffer;
            bd.keepInitialState = true;
            bd.debugName = "preview vertices";
            ps.vb = dev_.device()->createBuffer(bd);
        }
        cl_->writeBuffer(ps.vb, r.tris.data(), bytes);
        cl_->writeTexture(ps.atlas, 0, 0, r.atlas.data(), size_t(r.atlasW) * 4);
        cl_->clearTextureFloat(ps.color, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
        cl_->clearDepthStencilTexture(ps.depth, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        nvrhi::GraphicsState gs;
        gs.pipeline = previewPipe_;
        gs.framebuffer = ps.fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(ps.w), float(ps.h)));
        gs.bindings = {ps.set};
        gs.vertexBuffers = {{ps.vb, 0, 0}};
        cl_->setGraphicsState(gs);
        PreviewConsts c{};
        std::memcpy(&c.viewProj, r.viewProj, sizeof c.viewProj);
        std::memcpy(c.eye, r.eye, sizeof r.eye);
        std::memcpy(c.light, r.light, sizeof r.light);
        cl_->setPushConstants(&c, sizeof c);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(uint32_t(r.tris.size())));
    }

    // sceneGrab's copy: the requested rect clipped to the back buffer, at the same offset in the grab texture
    void copyGrab(nvrhi::ITexture* back) {
        grabPending_ = false;
        if (!grabTex_) return;
        const auto& bd = back->getDesc();
        const int x0 = std::max(grabX_, 0), y0 = std::max(grabY_, 0);
        const int x1 = std::min(grabX_ + grabW_, int(bd.width)), y1 = std::min(grabY_ + grabH_, int(bd.height));
        if (x1 <= x0 || y1 <= y0) return;
        nvrhi::TextureSlice dst, src;
        dst.setOrigin(uint32_t(x0 - grabX_), uint32_t(y0 - grabY_)).setSize(uint32_t(x1 - x0), uint32_t(y1 - y0), 1);
        src.setOrigin(uint32_t(x0), uint32_t(y0)).setSize(uint32_t(x1 - x0), uint32_t(y1 - y0), 1);
        cl_->copyTexture(grabTex_, dst, back, src);
    }

    struct Targets {
        nvrhi::TextureHandle color, motion, normal, reactive, depth, upscaled;
        // aa=msaa: the scene draws into these (and a multisampled depth), resolved into color..reactive
        nvrhi::TextureHandle msColor, msMotion, msNormal, msReactive, msGlow;
        nvrhi::TextureHandle glow; // the light the glowing effects add (scene.hlsl PSOut::glow): the bloom's source
        int samples = 1;
        nvrhi::FramebufferHandle scene;
        nvrhi::FramebufferHandle post; // color + reactive at render size (after the MSAA resolve): the blown snow
        nvrhi::FramebufferHandle colorOnly; // color at render size (after the resolve): the bloom's last pass
        nvrhi::FramebufferHandle lines;     // the scene's colour + reactive (multisampled with aa=msaa): the anime line
        // the bloom's halved copies of glow, made when first needed (drawBloom)
        nvrhi::TextureHandle bloom[kBloomLevels];
        nvrhi::FramebufferHandle bloomFb[kBloomLevels];
        nvrhi::BindingSetHandle bloomDown[kBloomLevels], bloomUp[kBloomLevels]; // level k's source: the pass into it
        int renderW = 0, renderH = 0, outW = 0, outH = 0;
    };

    nvrhi::TextureHandle makeTarget(int w, int h, nvrhi::Format f, const char* name, bool depth = false, int samples = 1) {
        nvrhi::TextureDesc td;
        td.sampleCount = uint32_t(samples);
        if (samples > 1) td.dimension = nvrhi::TextureDimension::Texture2DMS;
        td.width = uint32_t(w);
        td.height = uint32_t(h);
        td.format = f;
        td.isRenderTarget = true;
        td.isShaderResource = true;
        td.initialState = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
        td.keepInitialState = true;
        td.setClearValue(nvrhi::Color(depth ? 1.0f : 0.0f));
        td.debugName = name;
        return dev_.device()->createTexture(td);
    }

    // render-size G-buffer + output-size upscaled target; pipelines are made once (formats never change)
    void createTargets(int rw, int rh) {
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        const int ow = dev_.width(), oh = dev_.height();
        const int prevSamples = t_.samples, samples = up_ ? up_->sampleCount() : 1;
        t_ = {};
        t_.samples = samples;
        t_.renderW = rw;
        t_.renderH = rh;
        t_.outW = ow;
        t_.outH = oh;
        t_.color = makeTarget(rw, rh, nvrhi::Format::RGBA16_FLOAT, "color");
        t_.motion = makeTarget(rw, rh, nvrhi::Format::RG16_FLOAT, "motion");
        t_.normal = makeTarget(rw, rh, nvrhi::Format::R10G10B10A2_UNORM, "normal");
        t_.reactive = makeTarget(rw, rh, nvrhi::Format::R8_UNORM, "reactive");
        t_.glow = makeTarget(rw, rh, kGlowFormat, "glow");
        t_.depth = makeTarget(rw, rh, kDepthFormat, "depth", true, samples);
        if (samples > 1) {
            t_.msGlow = makeTarget(rw, rh, kGlowFormat, "ms glow", false, samples);
            t_.msColor = makeTarget(rw, rh, nvrhi::Format::RGBA16_FLOAT, "ms color", false, samples);
            t_.msMotion = makeTarget(rw, rh, nvrhi::Format::RG16_FLOAT, "ms motion", false, samples);
            t_.msNormal = makeTarget(rw, rh, nvrhi::Format::R10G10B10A2_UNORM, "ms normal", false, samples);
            t_.msReactive = makeTarget(rw, rh, nvrhi::Format::R8_UNORM, "ms reactive", false, samples);
        }
        {
            nvrhi::TextureDesc ud;
            ud.width = uint32_t(ow);
            ud.height = uint32_t(oh);
            ud.format = nvrhi::Format::RGBA16_FLOAT;
            ud.isShaderResource = true;
            ud.isUAV = true; // written by the upscalers' compute passes
            ud.initialState = nvrhi::ResourceStates::ShaderResource;
            ud.keepInitialState = true;
            ud.debugName = "upscaled";
            t_.upscaled = d->createTexture(ud);
        }
        const bool ms = samples > 1;
        t_.scene = d->createFramebuffer(nvrhi::FramebufferDesc()
                                            .addColorAttachment(ms ? t_.msColor : t_.color)
                                            .addColorAttachment(ms ? t_.msMotion : t_.motion)
                                            .addColorAttachment(ms ? t_.msNormal : t_.normal)
                                            .addColorAttachment(ms ? t_.msReactive : t_.reactive)
                                            .addColorAttachment(ms ? t_.msGlow : t_.glow)
                                            .setDepthAttachment(t_.depth));
        t_.post = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(t_.color).addColorAttachment(t_.reactive));
        t_.colorOnly = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(t_.color));
        t_.lines = d->createFramebuffer(nvrhi::FramebufferDesc()
                                            .addColorAttachment(ms ? t_.msColor : t_.color)
                                            .addColorAttachment(ms ? t_.msReactive : t_.reactive));
        outlineSet_ = nullptr; // it binds the old normal target
        blizzardSet_ = nullptr; // it binds the old depth copy
        if (!blizzardPipe_) { // the blown snow over the resolved colour: premultiplied (One / SrcAlpha), reactive = max
            nvrhi::GraphicsPipelineDesc bp;
            bp.primType = nvrhi::PrimitiveType::TriangleList;
            bp.VS = fullVs_;
            bp.PS = blizzardPs_;
            bp.bindingLayouts = {precipLayout_};
            bp.renderState.rasterState.setCullNone();
            bp.renderState.depthStencilState.depthTestEnable = false;
            bp.renderState.depthStencilState.depthWriteEnable = false;
            auto& c = bp.renderState.blendState.targets[0];
            c.blendEnable = true;
            c.srcBlend = nvrhi::BlendFactor::One;
            c.destBlend = nvrhi::BlendFactor::SrcAlpha;
            c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
            auto& r = bp.renderState.blendState.targets[1];
            r.blendEnable = true;
            r.srcBlend = r.destBlend = r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
            r.blendOp = r.blendOpAlpha = nvrhi::BlendOp::Max;
            blizzardPipe_ = d->createGraphicsPipeline(bp, t_.post->getFramebufferInfo());
        }
        if (samples != prevSamples) opaque_ = nullptr; // the scene pipelines carry the sample count
        outSets_.clear();
        debugSets_.clear();
        backFbs_.clear();
        if (!opaque_) createScenePipelines();
    }

    void createScenePipelines() {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::GraphicsPipelineDesc pd;
        pd.primType = nvrhi::PrimitiveType::TriangleList;
        pd.inputLayout = inputLayout_;
        pd.VS = sceneVs_;
        pd.PS = scenePs_;
        pd.bindingLayouts = {sceneLayout_};
        pd.renderState.rasterState.setCullNone(); // PS1 strips have inconsistent winding
        auto& ds = pd.renderState.depthStencilState;
        ds.depthTestEnable = true;
        ds.depthWriteEnable = true;
        ds.depthFunc = nvrhi::ComparisonFunc::Less;
        // the glow target takes only the light the additive draws add (the semi pipelines of B + F and B + F/4 below)
        pd.renderState.blendState.targets[4].colorWriteMask = nvrhi::ColorMask(0);
        const nvrhi::FramebufferInfo& fb = t_.scene->getFramebufferInfo();
        opaque_ = d->createGraphicsPipeline(pd, fb);

        nvrhi::GraphicsPipelineDesc sky = pd; // drawn first, behind everything, no depth
        sky.renderState.depthStencilState.depthTestEnable = false;
        sky.renderState.depthStencilState.depthWriteEnable = false;
        sky_pipe_ = d->createGraphicsPipeline(sky, fb);

        // PS1 blend modes on (B)ackground and (F)oreground as the PC exe sets them (0x402280): 0 B/2+F/2, 1 B+F,
        // 2 B*(1-F), 3 B+F/4 -- the fragment's alpha is the exe's vertex alpha (0x402100: 0x80 / 0x40, see
        // scene.hlsl blendMode; not the blend constant: NVRHI sets OMSetBlendFactor only when the value changes
        // since the last state, so the opaque draws' value never reached the semi pipelines and it stayed 1 = B+F);
        // no motion / normal, reactive = max(dst, this draw's value); depth test, no depth write
        for (int mode = 0; mode < 4; ++mode) {
            nvrhi::GraphicsPipelineDesc sp = pd;
            sp.renderState.depthStencilState.depthWriteEnable = false;
            auto& c = sp.renderState.blendState.targets[0];
            c.blendEnable = true;
            using BF = nvrhi::BlendFactor;
            static constexpr BF kSrc[4] = {BF::SrcAlpha, BF::One, BF::Zero, BF::SrcAlpha};
            static constexpr BF kDst[4] = {BF::InvSrcAlpha, BF::One, BF::InvSrcColor, BF::One};
            c.srcBlend = kSrc[mode];
            c.destBlend = kDst[mode];
            c.blendOp = nvrhi::BlendOp::Add;
            c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
            c.srcBlendAlpha = nvrhi::BlendFactor::One;
            c.destBlendAlpha = nvrhi::BlendFactor::Zero;
            sp.renderState.blendState.targets[1].colorWriteMask = nvrhi::ColorMask(0);
            sp.renderState.blendState.targets[2].colorWriteMask = nvrhi::ColorMask(0);
            auto& r = sp.renderState.blendState.targets[3];
            r.blendEnable = true;
            r.srcBlend = r.destBlend = nvrhi::BlendFactor::One;
            r.blendOp = nvrhi::BlendOp::Max;
            r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
            r.blendOpAlpha = nvrhi::BlendOp::Max;
            if (mode == 1 || mode == 3) { // the light added (scene.hlsl PSOut::glow gives it as it reaches the colour)
                auto& g = sp.renderState.blendState.targets[4];
                g.blendEnable = true;
                g.srcBlend = g.destBlend = g.srcBlendAlpha = g.destBlendAlpha = nvrhi::BlendFactor::One;
                g.colorWriteMask = nvrhi::ColorMask::All;
            }
            semi_[mode] = d->createGraphicsPipeline(sp, fb);
        }

        // the precipitation's flakes (no vertex buffer: made from the vertex id): alpha blended over the scene, behind
        // what is nearer (depth test, no depth write), no motion / normal, reactive = max
        nvrhi::GraphicsPipelineDesc fp;
        fp.primType = nvrhi::PrimitiveType::TriangleList;
        fp.VS = flakeVs_;
        fp.PS = flakePs_;
        fp.bindingLayouts = {precipLayout_};
        fp.renderState.rasterState.setCullNone();
        fp.renderState.depthStencilState.depthTestEnable = true;
        fp.renderState.depthStencilState.depthWriteEnable = false;
        fp.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
        auto& c = fp.renderState.blendState.targets[0];
        c.blendEnable = true;
        c.srcBlend = nvrhi::BlendFactor::SrcAlpha;
        c.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
        fp.renderState.blendState.targets[1].colorWriteMask = nvrhi::ColorMask(0);
        fp.renderState.blendState.targets[2].colorWriteMask = nvrhi::ColorMask(0);
        fp.renderState.blendState.targets[4].colorWriteMask = nvrhi::ColorMask(0);
        auto& r = fp.renderState.blendState.targets[3];
        r.blendEnable = true;
        r.srcBlend = r.destBlend = r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
        r.blendOp = r.blendOpAlpha = nvrhi::BlendOp::Max;
        flakePipe_ = d->createGraphicsPipeline(fp, fb);

        // the anime look's line (scene.hlsl ps_outline) over the opaque scene: alpha blended, reactive = max
        nvrhi::GraphicsPipelineDesc lp;
        lp.primType = nvrhi::PrimitiveType::TriangleList;
        lp.VS = fullVs_;
        lp.PS = outlinePs_[t_.samples > 1 ? 1 : 0];
        lp.bindingLayouts = {outlineLayout_};
        lp.renderState.rasterState.setCullNone();
        lp.renderState.depthStencilState.depthTestEnable = false;
        lp.renderState.depthStencilState.depthWriteEnable = false;
        auto& lc = lp.renderState.blendState.targets[0];
        lc.blendEnable = true;
        lc.srcBlend = nvrhi::BlendFactor::SrcAlpha;
        lc.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        lc.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
        auto& lr = lp.renderState.blendState.targets[1];
        lr.blendEnable = true;
        lr.srcBlend = lr.destBlend = lr.srcBlendAlpha = lr.destBlendAlpha = nvrhi::BlendFactor::One;
        lr.blendOp = lr.blendOpAlpha = nvrhi::BlendOp::Max;
        outlinePipe_ = d->createGraphicsPipeline(lp, t_.lines->getFramebufferInfo());
    }

    // the anime look's line, once the opaque surfaces and the copy of their depth are drawn (the frame constants written)
    void drawOutline() {
        if (!outlineSet_) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(5, depthCopy_),
                           nvrhi::BindingSetItem::Texture_SRV(6, sceneCopy_),
                           nvrhi::BindingSetItem::Texture_SRV(11, t_.samples > 1 ? t_.msNormal : t_.normal)};
            outlineSet_ = dev_.device()->createBindingSet(bs, outlineLayout_);
        }
        // the scene so far, for the colour of what the line goes around (the see-through's lens copies over it later)
        if (t_.samples > 1) cl_->resolveTexture(sceneCopy_, nvrhi::AllSubresources, t_.msColor, nvrhi::AllSubresources);
        else cl_->copyTexture(sceneCopy_, nvrhi::TextureSlice(), t_.color, nvrhi::TextureSlice());
        nvrhi::GraphicsState gs;
        gs.pipeline = outlinePipe_;
        gs.framebuffer = t_.lines;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {outlineSet_};
        cl_->setGraphicsState(gs);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    // The precipitation's height map (renderer.hpp Precipitation): the room's surfaces seen from above (an orthographic
    // depth map, the highest surface per texel), over the room's x / z bounds at kHeightTexels on the longer side.
    // No geometry: one texel without a surface
    void buildHeightMap(const std::vector<RenderVertex>& tris) {
        nvrhi::IDevice* d = dev_.device();
        float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const RenderVertex& v : tris)
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], v.pos[c]);
                hi[c] = std::max(hi[c], v.pos[c]);
            }
        const bool any = !tris.empty() && hi[0] > lo[0] && hi[2] > lo[2];
        int tw = 1, th = 1;
        if (any) {
            const float texel = std::max(hi[0] - lo[0], hi[2] - lo[2]) / float(kHeightTexels);
            tw = std::clamp(int(std::ceil((hi[0] - lo[0]) / texel)), 1, kHeightTexels);
            th = std::clamp(int(std::ceil((hi[2] - lo[2]) / texel)), 1, kHeightTexels);
            // a margin above / below: a surface exactly at the bottom keeps a depth below 1 (1 = no surface)
            heightBounds_[0] = lo[0];
            heightBounds_[1] = lo[2];
            heightBounds_[2] = 1.0f / (hi[0] - lo[0]);
            heightBounds_[3] = 1.0f / (hi[2] - lo[2]);
            heightRange_[0] = hi[1] + 1.0f;
            heightRange_[1] = hi[1] - lo[1] + 2.0f;
        } else {
            std::fill(std::begin(heightBounds_), std::end(heightBounds_), 0.0f);
            heightRange_[0] = 0.0f;
            heightRange_[1] = 1.0f;
        }
        heightRange_[2] = float(tw);
        heightRange_[3] = float(th);
        flakeSet_ = blizzardSet_ = nullptr; // they bind the old map
        height_ = makeTarget(tw, th, nvrhi::Format::D32, "precipitation height map", true);
        if (!height_) return;
        nvrhi::FramebufferHandle fb = d->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(height_));
        if (!heightPipe_) {
            nvrhi::GraphicsPipelineDesc hp;
            hp.primType = nvrhi::PrimitiveType::TriangleList;
            hp.inputLayout = inputLayout_;
            hp.VS = heightVs_;
            hp.PS = heightPs_;
            hp.bindingLayouts = {sceneLayout_};
            hp.renderState.rasterState.setCullNone();
            hp.renderState.depthStencilState.depthTestEnable = true;
            hp.renderState.depthStencilState.depthWriteEnable = true;
            hp.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less; // the highest surface
            heightPipe_ = d->createGraphicsPipeline(hp, fb->getFramebufferInfo());
        }
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->clearDepthStencilTexture(height_, nvrhi::AllSubresources, true, 1.0f, false, 0);
        if (any && vbuf_ && sceneSet_ && heightPipe_) {
            FrameConstants f{};
            std::copy(std::begin(heightBounds_), std::end(heightBounds_), f.heightBounds);
            std::copy(std::begin(heightRange_), std::end(heightRange_), f.heightRange);
            cl->writeBuffer(frameCb_, &f, sizeof f);
            nvrhi::GraphicsState gs;
            gs.pipeline = heightPipe_;
            gs.framebuffer = fb;
            gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(tw), float(th)));
            gs.bindings = {sceneSet_};
            gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vbuf_).setSlot(0).setOffset(0)};
            cl->setGraphicsState(gs);
            cl->draw(nvrhi::DrawArguments().setVertexCount(vertexCount_));
        }
        cl->close();
        d->executeCommandList(cl);
    }

    bool precipitating() const { return precip_.snow > 0.0f || precip_.blizzard > 0.0f; }

    // the flakes' / the blown snow's bindings: the frame, the height map, the depth copy
    nvrhi::BindingSetHandle precipSet() {
        nvrhi::BindingSetDesc bs;
        bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(4, height_),
                       nvrhi::BindingSetItem::Texture_SRV(5, depthCopy_)};
        return dev_.device()->createBindingSet(bs, precipLayout_);
    }

    // the flakes' light: the place's (its fog colour, or the background) -- a dark place has dim snow
    float precipLight() const {
        const float* c = fog_.mode != FogMode::Off ? fog_.color : clear_;
        return std::clamp(0.3f + 0.8f * std::max({c[0], c[1], c[2]}), 0.45f, 1.0f);
    }

    // the blown snow over the resolved scene colour (Precipitation::blizzard), before the upscaler
    void drawBlowingSnow() {
        copyDepth();
        if (!blizzardSet_) blizzardSet_ = precipSet();
        writeFrame(lastVpJit_, lastVp_, prevVp_, lastEye_, lastEye_, lastFwd_, 1.0f, true, 0.0f, 0.0f);
        nvrhi::GraphicsState gs;
        gs.pipeline = blizzardPipe_;
        gs.framebuffer = t_.post;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {blizzardSet_};
        cl_->setGraphicsState(gs);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    void upscale(const Camera& cam) {
        const auto now = std::chrono::steady_clock::now();
        const float dtMs = lastFrame_.time_since_epoch().count()
                               ? std::chrono::duration<float, std::milli>(now - lastFrame_).count()
                               : 16.7f;
        lastFrame_ = now;
        UpscalerInputs in{};
        in.color = t_.color;
        in.depth = copyDepth(); // R32F: every SDK reads float depth, D24S8 views are not portable
        in.motion = t_.motion;
        in.reactive = t_.reactive;
        in.output = t_.upscaled;
        in.jitterX = jitter_.x;
        in.jitterY = jitter_.y;
        in.renderW = t_.renderW;
        in.renderH = t_.renderH;
        in.outW = t_.outW;
        in.outH = t_.outH;
        in.frameTimeMs = std::clamp(dtMs, 1.0f, 100.0f);
        in.nearZ = cam.nearZ;
        in.farZ = kFar;
        in.fovY = cam.fovY;
        in.reset = pendingReset_;
        if (pendingReset_) SDL_Log("aa: history reset");
        pendingReset_ = false;
        up_->evaluate(cl_, in);
        cl_->clearState(); // vendor SDKs change heaps / root signatures behind NVRHI
    }

    // scene.hlsl / output.hlsl waterMedium: the far water along a view direction of height dirY
    static void waterMedium(const float tint[3], float dirY, float out[3]) {
        const float m = std::max({tint[0], tint[1], tint[2], 1e-3f});
        float t = std::clamp((dirY + 0.65f) / 1.5f, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        const float lo[3] = {0.025f, 0.08f, 0.12f}, hi[3] = {0.15f, 0.38f, 0.46f};
        for (int k = 0; k < 3; ++k) out[k] = tint[k] / m * (lo[k] + (hi[k] - lo[k]) * t);
    }
    // scene.hlsl / output.hlsl heatMedium: the hot air far away (lava glow at the horizon, dark smoke above)
    static void heatMedium(const float tint[3], float dirY, float out[3]) {
        const float m = std::max({tint[0], tint[1], tint[2], 1e-3f});
        float t = std::clamp((dirY + 0.3f) / 1.0f, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        const float lo[3] = {0.34f, 0.13f, 0.05f}, hi[3] = {0.06f, 0.03f, 0.025f};
        for (int k = 0; k < 3; ++k) out[k] = (1.0f + (tint[k] / m - 1.0f) * 0.3f) * (lo[k] + (hi[k] - lo[k]) * t);
    }

    // hot air under an open sky (the desert: sky layers or a bright background colour) keeps the sky; only the lava
    // caves' empty black void is filled with the hot air's smoke / glow
    bool heatOpenSky() const {
        return waveHeat_ && (!sky_.empty() || clear_[0] * 0.3f + clear_[1] * 0.59f + clear_[2] * 0.11f > 0.08f);
    }

    void clearTargets() {
        float clear[3] = {clear_[0], clear_[1], clear_[2]};
        // underwater (remaster): the empty background starts as the far water at the view's height, so the upscaler
        // and the blur never blend a black void into the edges of the room (the output pass paints the gradient)
        if (waveRemaster_ && wave_[0] > 0.0f && !heatOpenSky())
            (waveHeat_ ? heatMedium : waterMedium)(waveTint_, std::sin(camPitch_), clear);
        const bool ms = t_.samples > 1;
        cl_->clearTextureFloat(ms ? t_.msColor : t_.color, nvrhi::AllSubresources,
                               nvrhi::Color(clear[0], clear[1], clear[2], 1.0f));
        cl_->clearTextureFloat(ms ? t_.msMotion : t_.motion, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(ms ? t_.msNormal : t_.normal, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(ms ? t_.msReactive : t_.reactive, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(ms ? t_.msGlow : t_.glow, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearDepthStencilTexture(t_.depth, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        depthFresh_ = false;
    }

    // fullscreen debugview pass of src (mode, see debugview.hlsl) into fb
    void debugPass(nvrhi::ITexture* src, int mode, nvrhi::IFramebuffer* fb, float w, float h) {
        const nvrhi::FramebufferInfo& info = fb->getFramebufferInfo();
        const bool ms = src->getDesc().sampleCount > 1; // the MSAA depth: debugview.hlsl OD_MSAA
        nvrhi::GraphicsPipelineHandle& pipe =
            debugPipes_[(info.colorFormats.empty() ? 0 : int(info.colorFormats[0])) * 2 + (ms ? 1 : 0)];
        if (!pipe) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.VS = debugVs_;
            pd.PS = ms ? debugMsPs_ : debugPs_;
            pd.bindingLayouts = {debugLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.depthStencilState.depthWriteEnable = false;
            pipe = dev_.device()->createGraphicsPipeline(pd, info);
        }
        auto it = debugSets_.find(src);
        if (it == debugSets_.end()) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(int) * 4),
                           nvrhi::BindingSetItem::Texture_SRV(0, src), nvrhi::BindingSetItem::Sampler(0, pointClamp_)};
            it = debugSets_.emplace(src, dev_.device()->createBindingSet(bs, debugLayout_)).first;
        }
        nvrhi::GraphicsState gs;
        gs.pipeline = pipe;
        gs.framebuffer = fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(w, h));
        gs.bindings = {it->second};
        cl_->setGraphicsState(gs);
        const int params[4] = {mode, 0, 0, 0};
        cl_->setPushConstants(params, sizeof params);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    void drawDebug(nvrhi::ITexture* back) {
        nvrhi::ITexture* src = t_.color;
        switch (view_) {
        case DebugView::Motion: src = t_.motion; break;
        case DebugView::Normal: src = t_.normal; break;
        case DebugView::Depth: src = t_.depth; break;
        case DebugView::Reactive: src = t_.reactive; break;
        default: break;
        }
        debugPass(src, int(view_) - 1, backFramebuffer(back), float(dev_.width()), float(dev_.height()));
    }

    // the R32F copy of the depth (render size): the soft effects, the precipitation, the upscalers and the CPU readback
    // read it; made / resized with the binding sets that bind it
    bool depthCopyStale() const {
        return !depthCopy_ || depthCopy_->getDesc().width != uint32_t(t_.renderW) ||
               depthCopy_->getDesc().height != uint32_t(t_.renderH);
    }
    void makeDepthCopy() {
        depthCopy_ = makeTarget(t_.renderW, t_.renderH, nvrhi::Format::R32_FLOAT, "depth copy");
        depthCopyFb_ = dev_.device()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(depthCopy_));
        sceneCopy_ = makeTarget(t_.renderW, t_.renderH, t_.color->getDesc().format, "scene colour copy");
        outSets_.clear(); // they bind the old copy
        flakeSet_ = blizzardSet_ = nullptr;
        outlineSet_ = nullptr;
        light_ = nullptr; // the traced light's targets are of the old size: made again when next traced
        preDepth_ = nullptr;
        makeSceneSet();
    }

    // The traced light (scene.hlsl lightTex): the sun's soft shadows and the ambient occlusion
    bool sunSoft() const { return rt_ && sun_.enabled && !sun_.sheltered && sun_.softness > 0.0f; }
    bool occluding() const { return rt_ && occlusion_.enabled && occlusion_.strength > 0.0f; }

    // its targets at render size (the prepass's depth and normals, the rays' result and its two blur passes), made when
    // a frame first traces; the scene's set binds the result
    void makeLightTargets() {
        nvrhi::IDevice* d = dev_.device();
        const int w = t_.renderW, h = t_.renderH;
        preDepth_ = makeTarget(w, h, nvrhi::Format::R32_FLOAT, "light prepass depth");
        preNormal_ = makeTarget(w, h, nvrhi::Format::RGBA8_SNORM, "light prepass normals");
        preZ_ = makeTarget(w, h, kDepthFormat, "light prepass z", true);
        preFb_ = d->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(preDepth_).addColorAttachment(preNormal_).setDepthAttachment(preZ_));
        static const char* const kNames[3] = {"light rays", "light blur", "light"};
        for (int k = 0; k < 3; ++k) {
            lightPass_[k] = makeTarget(w, h, nvrhi::Format::RG8_UNORM, kNames[k]);
            lightFb_[k] = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(lightPass_[k]));
        }
        light_ = lightPass_[2];
        if (!prePipe_) {
            nvrhi::GraphicsPipelineDesc pd; // the opaque draws' rasterisation (createScenePipelines)
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.inputLayout = inputLayout_;
            pd.VS = sceneVs_;
            pd.PS = prePs_;
            pd.bindingLayouts = {preLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = true;
            pd.renderState.depthStencilState.depthWriteEnable = true;
            pd.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
            prePipe_ = d->createGraphicsPipeline(pd, preFb_->getFramebufferInfo());
            nvrhi::GraphicsPipelineDesc fp;
            fp.primType = nvrhi::PrimitiveType::TriangleList;
            fp.VS = fullVs_;
            fp.renderState.rasterState.setCullNone();
            fp.renderState.depthStencilState.depthTestEnable = false;
            fp.renderState.depthStencilState.depthWriteEnable = false;
            fp.PS = lightPs_;
            fp.bindingLayouts = {lightLayout_};
            lightPipe_[0] = d->createGraphicsPipeline(fp, lightFb_[0]->getFramebufferInfo());
            fp.bindingLayouts = {lightBlurLayout_};
            for (int k = 0; k < 2; ++k) {
                fp.PS = lightBlurPs_[k];
                lightPipe_[k + 1] = d->createGraphicsPipeline(fp, lightFb_[k + 1]->getFramebufferInfo());
            }
        }
        makeSceneSet();
    }

    // the prepass's and the three passes' sets (after makeSceneSet: the pages, the ray-tracing buffers or the targets
    // changed)
    void makeLightSets() {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::BindingSetDesc bs;
        bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(0, pages_),
                       nvrhi::BindingSetItem::Sampler(0, pointClamp_)};
        preSet_ = d->createBindingSet(bs, preLayout_);
        bs.bindings.push_back(nvrhi::BindingSetItem::RayTracingAccelStruct(1, tlas_));
        bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, staticAttr_));
        bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, dynAttr_));
        bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(8, preDepth_));
        bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(9, preNormal_));
        lightSet_[0] = d->createBindingSet(bs, lightLayout_);
        for (int k = 0; k < 2; ++k) {
            bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(8, preDepth_),
                           nvrhi::BindingSetItem::Texture_SRV(9, preNormal_),
                           nvrhi::BindingSetItem::Texture_SRV(10, lightPass_[k])};
            lightSet_[k + 1] = d->createBindingSet(bs, lightBlurLayout_);
        }
    }

    // this frame's traced light, before the opaque draws that read it: the prepass (their surfaces, drawn as they
    // draw them), the rays, the blur. `opaque` writes the frame constants of the opaque draws
    void drawLight(const std::function<void()>& opaque) {
        if (!light_ || !preDepth_) makeLightTargets();
        if (!preSet_) makeLightSets();
        cl_->clearTextureFloat(preDepth_, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(preNormal_, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearDepthStencilTexture(preZ_, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        nvrhi::GraphicsState gs;
        gs.pipeline = prePipe_;
        gs.framebuffer = preFb_;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {preSet_};
        const auto range = [&](nvrhi::IBuffer* vb, uint32_t first, uint32_t count) {
            if (!count) return;
            gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vb).setSlot(0).setOffset(0)};
            cl_->setGraphicsState(gs);
            cl_->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
        };
        const uint32_t dynCount = dynCount_, dynOpaque = std::min(dynStart_[0], dynCount);
        opaque();
        range(vbuf_, 0, semiStart_[0]);
        if (dynCount) range(dynDrawn(), 0, dynOpaque);
        frameSemiPass_ = 1.0f; // the semi-transparent ranges' opaque texels (drawScene)
        opaque();
        range(vbuf_, semiStart_[0], semiStart_[3] - semiStart_[0]);
        if (dynOpaque < dynCount) range(dynDrawn(), dynOpaque, dynCount - dynOpaque);
        frameSemiPass_ = 0.0f;
        for (int k = 0; k < 3; ++k) {
            nvrhi::GraphicsState fs;
            fs.pipeline = lightPipe_[k];
            fs.framebuffer = lightFb_[k];
            fs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
            fs.bindings = {lightSet_[k]};
            cl_->setGraphicsState(fs);
            cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
        }
    }

    // The bloom (bloom.hlsl): the glow target halved kBloomLevels times, each copy then enlarged and added to the one
    // before it, the last onto the scene's colour (render size, before the upscaler)
    void drawBloom() {
        nvrhi::IDevice* d = dev_.device();
        int w[kBloomLevels + 1] = {t_.renderW}, h[kBloomLevels + 1] = {t_.renderH};
        for (int k = 1; k <= kBloomLevels; ++k) {
            w[k] = std::max(w[k - 1] / 2, 1);
            h[k] = std::max(h[k - 1] / 2, 1);
        }
        const auto set = [&](nvrhi::ITexture* src) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(BloomConstants)),
                           nvrhi::BindingSetItem::Texture_SRV(0, src), nvrhi::BindingSetItem::Sampler(0, linearClamp_)};
            return d->createBindingSet(bs, bloomLayout_);
        };
        if (!t_.bloom[0]) {
            for (int k = 0; k < kBloomLevels; ++k) {
                t_.bloom[k] = makeTarget(w[k + 1], h[k + 1], kGlowFormat, "bloom");
                t_.bloomFb[k] = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(t_.bloom[k]));
            }
            for (int k = 0; k < kBloomLevels; ++k) {
                t_.bloomDown[k] = set(k ? t_.bloom[k - 1].Get() : t_.glow.Get());
                if (k + 1 < kBloomLevels) t_.bloomUp[k] = set(t_.bloom[k + 1]);
            }
            bloomLast_ = set(t_.bloom[0]);
        }
        if (!bloomPipe_[0]) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.VS = fullVs_;
            pd.PS = bloomPs_[0];
            pd.bindingLayouts = {bloomLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.depthStencilState.depthWriteEnable = false;
            bloomPipe_[0] = d->createGraphicsPipeline(pd, t_.bloomFb[0]->getFramebufferInfo());
            pd.PS = bloomPs_[1];
            auto& c = pd.renderState.blendState.targets[0];
            c.blendEnable = true;
            c.srcBlend = c.destBlend = nvrhi::BlendFactor::One;
            c.srcBlendAlpha = nvrhi::BlendFactor::Zero;
            c.destBlendAlpha = nvrhi::BlendFactor::One;
            c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
            bloomPipe_[1] = d->createGraphicsPipeline(pd, t_.bloomFb[0]->getFramebufferInfo());
            bloomPipe_[2] = d->createGraphicsPipeline(pd, t_.colorOnly->getFramebufferInfo());
        }
        const auto pass = [&](nvrhi::IGraphicsPipeline* pipe, nvrhi::IFramebuffer* fb, nvrhi::IBindingSet* from, int target,
                              int source, float gain) {
            nvrhi::GraphicsState gs;
            gs.pipeline = pipe;
            gs.framebuffer = fb;
            gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(w[target]), float(h[target])));
            gs.bindings = {from};
            cl_->setGraphicsState(gs);
            const BloomConstants c{{1.0f / float(w[target]), 1.0f / float(h[target])},
                                   {1.0f / float(w[source]), 1.0f / float(h[source])}, gain, {}};
            cl_->setPushConstants(&c, sizeof c);
            cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
        };
        for (int k = 0; k < kBloomLevels; ++k) pass(bloomPipe_[0], t_.bloomFb[k], t_.bloomDown[k], k + 1, k, 1.0f);
        for (int k = kBloomLevels - 2; k >= 0; --k) pass(bloomPipe_[1], t_.bloomFb[k], t_.bloomUp[k], k + 1, k + 2, 1.0f);
        pass(bloomPipe_[2], t_.colorOnly, bloomLast_, 0, 1, bloom_.strength * kBloomGain);
    }
    // D24 depth -> the R32F copy
    nvrhi::ITexture* copyDepth() {
        if (depthCopyStale()) makeDepthCopy();
        debugPass(t_.depth, 5, depthCopyFb_, float(t_.renderW), float(t_.renderH));
        depthFresh_ = true;
        return depthCopy_;
    }

    nvrhi::StagingTextureHandle stage(nvrhi::ITexture* tex) {
        nvrhi::TextureDesc sd; // plain readback copy: no render-target / clear-value flags
        sd.width = tex->getDesc().width;
        sd.height = tex->getDesc().height;
        sd.format = tex->getDesc().format;
        sd.initialState = nvrhi::ResourceStates::CopyDest;
        sd.keepInitialState = true;
        nvrhi::StagingTextureHandle s = dev_.device()->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);
        cl_->copyTexture(s, nvrhi::TextureSlice(), tex, nvrhi::TextureSlice());
        return s;
    }

    // RG16F / R32F staging texture -> floats, `channels` per pixel, rows tightly packed
    void readFloats(nvrhi::IStagingTexture* s, nvrhi::Format f, int channels, std::vector<float>& out) {
        size_t pitch = 0;
        const auto* p = static_cast<const uint8_t*>(
            dev_.device()->mapStagingTexture(s, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        const int w = t_.renderW, h = t_.renderH;
        out.assign(size_t(w) * size_t(h) * size_t(channels), 0.0f);
        if (!p) return;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w * channels; ++x) {
                const uint8_t* row = p + size_t(y) * pitch;
                float v;
                if (f == nvrhi::Format::R32_FLOAT) {
                    std::memcpy(&v, row + size_t(x) * 4, 4);
                } else { // half
                    uint16_t hbits;
                    std::memcpy(&hbits, row + size_t(x) * 2, 2);
                    v = PackedVector::XMConvertHalfToFloat(hbits);
                }
                out[size_t(y) * size_t(w) * size_t(channels) + size_t(x)] = v;
            }
        dev_.device()->unmapStagingTexture(s);
    }

    // the camera's own up vector (d fwd / d pitch): equal to (0, 1, 0) re-orthogonalised for |pitch| < 90 degrees, and
    // still defined when looking straight down (the travel map ST01: pitch -0x400, where (0, 1, 0) is degenerate)
    static XMVECTOR cameraUp(const Camera& cam) {
        const XMVECTOR up = XMVectorSet(-std::sin(cam.yaw) * std::sin(cam.pitch), std::cos(cam.pitch),
                                        -std::cos(cam.yaw) * std::sin(cam.pitch), 0.0f);
        if (cam.roll == 0.0f) return up;
        // rolled about the forward axis: up cos r + right sin r (right = up x fwd, left-handed)
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        const XMVECTOR right = XMVector3Cross(up, fwd);
        return XMVectorAdd(XMVectorScale(up, std::cos(cam.roll)), XMVectorScale(right, std::sin(cam.roll)));
    }
    XMMATRIX cameraViewProj(const Camera& cam) const {
        const XMVECTOR eye = XMVectorSet(cam.eye[0], cam.eye[1], cam.eye[2], 1.0f);
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        return XMMatrixLookToLH(eye, fwd, cameraUp(cam)) *
               XMMatrixPerspectiveFovLH(cam.fovY, float(t_.renderW) / float(t_.renderH), cam.nearZ, kFar);
    }

    nvrhi::IFramebuffer* backFramebuffer(nvrhi::ITexture* back) {
        auto it = backFbs_.find(back);
        if (it != backFbs_.end()) return it->second;
        nvrhi::FramebufferHandle fb = dev_.device()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(back));
        if (!outPipe_) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.VS = outVs_;
            pd.PS = outPs_;
            pd.bindingLayouts = {outLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.depthStencilState.depthWriteEnable = false;
            outPipe_ = dev_.device()->createGraphicsPipeline(pd, fb->getFramebufferInfo());
        }
        backFbs_[back] = fb;
        return fb;
    }

    // grow-only vertex buffer rewritten each frame through the command list
    void stream(nvrhi::BufferHandle& buf, const std::vector<GpuVertex>& v, const char* name, bool rtInput = false) {
        if (v.empty()) return;
        const uint64_t bytes = v.size() * sizeof(GpuVertex);
        if (!buf || buf->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.isVertexBuffer = true;
            bd.isAccelStructBuildInput = rtInput;
            bd.canHaveRawViews = true; // expandDynamic reads the dynamic ones
            bd.initialState = nvrhi::ResourceStates::VertexBuffer;
            bd.keepInitialState = true;
            bd.debugName = name;
            buf = dev_.device()->createBuffer(bd);
        }
        cl_->writeBuffer(buf, v.data(), bytes);
    }

    // the buffer the dynamic draws and the actors' BLAS read: the streamed vertices, or the ones expandDynamic made of them
    nvrhi::IBuffer* dynDrawn() const { return dynTris_.empty() ? dynBuf_.Get() : dynFineBuf_.Get(); }

    // the frame's subdivision (uploadDynamic's dynTris_): dynBuf_'s triangles into dynFineBuf_ and the rays' attributes
    // of its vertices into dynAttr_ (subdivide.hlsl)
    void expandDynamic() {
        nvrhi::IDevice* d = dev_.device();
        struct Params { // subdivide.hlsl
            uint32_t triCount, level;
            float amount;
            uint32_t pad;
        };
        if (!subdivPipe_) {
            subdivCs_ = d->createShader({nvrhi::ShaderType::Compute, "", "cs_main"}, g_subdivide_cs, sizeof(g_subdivide_cs));
            nvrhi::BindingLayoutDesc bl;
            bl.visibility = nvrhi::ShaderType::Compute;
            bl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(Params)), nvrhi::BindingLayoutItem::RawBuffer_SRV(0),
                           nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::RawBuffer_UAV(0),
                           nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1)};
            subdivLayout_ = d->createBindingLayout(bl);
            nvrhi::ComputePipelineDesc pd;
            pd.CS = subdivCs_;
            pd.bindingLayouts = {subdivLayout_};
            subdivPipe_ = d->createComputePipeline(pd);
        }
        if (const uint64_t bytes = uint64_t(dynCount_) * sizeof(GpuVertex); !dynFineBuf_ || dynFineBuf_->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.isVertexBuffer = true;
            bd.isAccelStructBuildInput = rt_;
            bd.canHaveUAVs = true;
            bd.canHaveRawViews = true;
            bd.initialState = nvrhi::ResourceStates::VertexBuffer;
            bd.keepInitialState = true;
            bd.debugName = "subdivided dynamic vertices";
            dynFineBuf_ = d->createBuffer(bd);
            subdivSet_ = nullptr;
        }
        if (const uint64_t floats = uint64_t(dynCount_) * 4; !dynAttr_ || dynAttr_->getDesc().byteSize < floats * sizeof(float)) {
            dynAttr_ = attrBuffer(std::vector<float>(size_t(floats + floats / 2), 0.0f), "dynamic rt attributes");
            subdivSet_ = nullptr;
            if (rt_) makeSceneSet(); // (the scene's and the traced light's sets bind it)
        }
        if (const uint64_t bytes = dynTris_.size() * sizeof(uint32_t); !dynTriBuf_ || dynTriBuf_->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.canHaveRawViews = true;
            bd.initialState = nvrhi::ResourceStates::ShaderResource;
            bd.keepInitialState = true;
            bd.debugName = "subdivision triangles";
            dynTriBuf_ = d->createBuffer(bd);
            subdivSet_ = nullptr;
        }
        cl_->writeBuffer(dynTriBuf_, dynTris_.data(), dynTris_.size() * sizeof(uint32_t));
        if (!subdivSet_ || subdivFrom_ != dynBuf_.Get() || subdivAttr_ != dynAttr_.Get()) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(Params)), nvrhi::BindingSetItem::RawBuffer_SRV(0, dynBuf_),
                           nvrhi::BindingSetItem::RawBuffer_SRV(1, dynTriBuf_), nvrhi::BindingSetItem::RawBuffer_UAV(0, dynFineBuf_),
                           nvrhi::BindingSetItem::StructuredBuffer_UAV(1, dynAttr_)};
            subdivSet_ = d->createBindingSet(bs, subdivLayout_);
            subdivFrom_ = dynBuf_.Get();
            subdivAttr_ = dynAttr_.Get();
        }
        nvrhi::ComputeState cs;
        cs.pipeline = subdivPipe_;
        cs.bindings = {subdivSet_};
        cl_->setComputeState(cs);
        const uint32_t count = uint32_t(dynTris_.size() / 3);
        const Params p{count, uint32_t(subdivision_.level), subdivision_.amount, 0};
        cl_->setPushConstants(&p, sizeof p);
        cl_->dispatch((count + 63) / 64);
    }

    // shared index buffer 0, 1, 2 ... (grow-only, written through cl when it grows)
    nvrhi::IBuffer* sequentialIndices(nvrhi::ICommandList* cl, uint32_t n) {
        if (!indices_ || indices_->getDesc().byteSize < uint64_t(n) * 4) {
            std::vector<uint32_t> seq(std::max<uint32_t>(n + n / 2, 1u << 16));
            for (uint32_t i = 0; i < seq.size(); ++i) seq[i] = i;
            nvrhi::BufferDesc bd;
            bd.byteSize = seq.size() * 4;
            bd.isAccelStructBuildInput = true;
            bd.initialState = nvrhi::ResourceStates::AccelStructBuildInput;
            bd.keepInitialState = true;
            bd.debugName = "rt sequential indices";
            indices_ = dev_.device()->createBuffer(bd);
            cl->writeBuffer(indices_, seq.data(), bd.byteSize);
        }
        return indices_;
    }

    // structured float4 buffer (shadow-ray attributes), sized for `attr`
    nvrhi::BufferHandle attrBuffer(const std::vector<float>& attr, const char* name) {
        nvrhi::BufferDesc bd;
        bd.byteSize = attr.size() * sizeof(float);
        bd.structStride = 16;
        bd.canHaveUAVs = true; // expandDynamic writes the dynamic ones
        bd.initialState = nvrhi::ResourceStates::ShaderResource;
        bd.keepInitialState = true;
        bd.debugName = name;
        return dev_.device()->createBuffer(bd);
    }
    void uploadBuffer(nvrhi::IBuffer* buf, const std::vector<float>& data) {
        nvrhi::CommandListHandle cl = dev_.device()->createCommandList();
        cl->open();
        cl->writeBuffer(buf, data.data(), data.size() * sizeof(float));
        cl->close();
        dev_.device()->executeCommandList(cl);
    }

    void makeSceneSet() {
        preSet_ = nullptr; // the traced light's sets bind the same pages and buffers (makeLightSets)
        for (nvrhi::BindingSetHandle& s : lightSet_) s = nullptr;
        if (!pages_) return;
        if (depthCopyStale()) {
            makeDepthCopy(); // (it makes the set)
            return;
        }
        nvrhi::BindingSetDesc bs;
        bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(0, pages_),
                       nvrhi::BindingSetItem::Sampler(0, pointClamp_),
                       nvrhi::BindingSetItem::Sampler(1, sliceSize_ > 256 ? anisoClamp_ : pointClamp_),
                       nvrhi::BindingSetItem::Texture_SRV(5, depthCopy_), nvrhi::BindingSetItem::Texture_SRV(6, sceneCopy_)};
        if (rt_) {
            bs.bindings.push_back(nvrhi::BindingSetItem::RayTracingAccelStruct(1, tlas_));
            bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, staticAttr_));
            bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, dynAttr_));
            bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(7, light_ ? light_ : lightNone_));
            bs.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(8, preDepth_ ? preDepth_ : preDepthNone_));
        }
        sceneSet_ = dev_.device()->createBindingSet(bs, sceneLayout_);
    }

    // this frame's acceleration structures: room BLAS (built at upload) + actors' BLAS (rebuilt from the opaque
    // dynamic vertices every frame) -> TLAS
    void buildScene(uint32_t dynOpaque) {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::rt::InstanceDesc inst[2];
        size_t n = 0;
        const auto flags = nvrhi::rt::InstanceFlags::TriangleCullDisable; // shadows from both sides of every face
        if (staticBlas_)
            inst[n++] = nvrhi::rt::InstanceDesc().setBLAS(staticBlas_).setInstanceID(0).setInstanceMask(kRayMaskRoom).setFlags(flags);
        const uint32_t casters = dynOpaque / 3 * 3;
        // the characters cast the sun's shadow and the particle lights', and close the ambient occlusion's rays
        nvrhi::IBuffer* const dynBuf = dynDrawn();
        if (((sun_.enabled && !sun_.sheltered) || !particleLights_.empty() || occluding()) && casters && dynBuf) {
            if (dynTris_.empty()) { // (a subdivided frame's are expandDynamic's)
                toAttr(dynGpu_, casters, dynAttrCpu_);
                if (dynAttr_->getDesc().byteSize < dynAttrCpu_.size() * sizeof(float)) {
                    std::vector<float> grown(dynAttrCpu_.size() * 3 / 2 + 4, 0.0f);
                    dynAttr_ = attrBuffer(grown, "dynamic rt attributes");
                    makeSceneSet();
                }
                cl_->writeBuffer(dynAttr_, dynAttrCpu_.data(), dynAttrCpu_.size() * sizeof(float));
            }
            // sized for the whole grow-only buffer
            if (!dynBlas_ || dynBlasFor_ != dynBuf || dynBlasBytes_ != dynBuf->getDesc().byteSize) {
                const uint32_t capacity = uint32_t(dynBuf->getDesc().byteSize / sizeof(GpuVertex)) / 3 * 3;
                const nvrhi::rt::GeometryDesc full = triangleGeometry(dynBuf, sequentialIndices(cl_, capacity), capacity);
                dynBlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                                    .addBottomLevelGeometry(full)
                                                    .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastBuild)
                                                    .setDebugName("actors blas"));
                dynBlasBytes_ = dynBuf->getDesc().byteSize;
                dynBlasFor_ = dynBuf;
            }
            const nvrhi::rt::GeometryDesc g = triangleGeometry(dynBuf, sequentialIndices(cl_, casters), casters);
            cl_->buildBottomLevelAccelStruct(dynBlas_, &g, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastBuild);
            inst[n++] = nvrhi::rt::InstanceDesc().setBLAS(dynBlas_).setInstanceID(1).setInstanceMask(kRayMaskActors).setFlags(flags);
        }
        cl_->buildTopLevelAccelStruct(tlas_, inst, n, nvrhi::rt::AccelStructBuildFlags::AllowEmptyInstances);
    }

    void writeFrame(const XMMATRIX& vp, const XMMATRIX& vpNoJit, const XMMATRIX& prevVp, const float* eye,
                    const float* prevEye, const float* fwd, float cullSign, bool fog, float reactive,
                    float motionMode, bool shadow = false) {
        FrameConstants f{};
        XMStoreFloat4x4(&f.viewProj, vp);
        XMStoreFloat4x4(&f.viewProjNoJit, vpNoJit);
        XMStoreFloat4x4(&f.prevViewProj, prevVp);
        for (int c = 0; c < 3; ++c) {
            f.eye[c] = eye[c];
            f.prevEye[c] = prevEye[c];
            f.fwd[c] = fwd[c];
            f.fogColor[c] = fog_.color[c];
        }
        f.cullSign = cullSign;
        f.modelNear = nearZ_;
        std::memcpy(f.seeBody, seeBody_, sizeof f.seeBody);
        f.seeInfo[0] = seeDepth_[0];
        f.seeInfo[1] = seeDepth_[1];
        f.seeInfo[2] = float(seePieces_.size());
        f.seeInfo[3] = seeDebug_ ? 1.0f : 0.0f;
        for (size_t k = 0; k < seePieces_.size(); ++k) {
            f.seePiece[k][0] = float(seePieces_[k].piece);
            f.seePiece[k][1] = seePieces_[k].open;
            f.seePiece[k][2] = seePieces_[k].whole ? 1.0f : 0.0f;
            f.seePiece[k][3] = float(seePieces_[k].bodies);
        }
        f.seeLens = frameLens_;
        f.seeClock = seeClock_;
        f.fogStart = fog_.start;
        f.fogEnd = fog_.end;
        f.fogMode = fog ? float(int(fog_.mode)) : 0.0f;
        // the remaster fog is a medium around the eye: the sky behind it fogs too, as a ray that never ends (else the
        // fogged scenery's silhouette cuts against a clear sky); the classic table fog never touched the sky
        if (frameSky_ > 0.0f && skyIsSky_ && fog_.mode == FogMode::Remaster) {
            f.fogMode = float(int(FogMode::Remaster));
            f.fogSky = frameSky_;
            XMStoreFloat4x4(&f.skyRay, skyRay_);
        }
        if (frameSky_ == 1.0f) {
            for (int c = 0; c < 3; ++c) {
                f.skyPoleUp[c] = skyPoles_.up[c];
                f.skyPoleDown[c] = skyPoles_.down[c];
            }
            f.skyPoleUp[3] = skyPoles_.fadeFrom;
            f.skyPoleDown[3] = skyPoles_.fadeTo;
        }
        // the sun in the sky: on the sky's draws (not ST01's travel map), never in a room with no sky
        if (frameSky_ > 0.0f && skyIsSky_ && !sun_.sheltered && sun_.disc > 0.0f) {
            f.sunDisc[0] = std::min(sun_.disc, 20.0f) * 3.14159265f / 180.0f;
            f.sunDisc[1] = frameSky_;
            XMStoreFloat4x4(&f.skyRay, skyRay_);
        }
        f.fogDensity = fog_.density;
        f.fogHeightFalloff = fog_.heightFalloff;
        f.fogMaxOpacity = fog_.maxOpacity;
        f.reactive = reactive;
        f.renderSize[0] = float(t_.renderW);
        f.renderSize[1] = float(t_.renderH);
        f.motionMode = motionMode;
        f.blendMode = frameBlend_;
        f.semiPass = frameSemiPass_;
        f.pageBias = std::log2(float(t_.renderW) / float(t_.outW));
        const float len = std::sqrt(sun_.dir[0] * sun_.dir[0] + sun_.dir[1] * sun_.dir[1] + sun_.dir[2] * sun_.dir[2]);
        for (int c = 0; c < 3; ++c) f.sunDir[c] = len > 0.0f ? sun_.dir[c] / len : 0.0f;
        f.sunStrength = sun_.strength;
        f.shadowMode = shadow && rt_ && sun_.enabled && len > 0.0f ? (sun_.sheltered ? 2.0f : 1.0f) : 0.0f;
        f.sunScatter = sun_.enabled && !sun_.sheltered && len > 0.0f ? 0.35f : 0.0f;
        if (shadow && !particleLights_.empty()) { // the draws of the opaque surfaces, as the sun's shadow
            for (size_t k = 0; k < particleLights_.size(); ++k) {
                const ParticleLight& l = particleLights_[k];
                for (int c = 0; c < 3; ++c) {
                    f.lightPos[k][c] = l.pos[c];
                    f.lightColor[k][c] = l.color[c];
                }
                f.lightPos[k][3] = l.radius;
                f.lightColor[k][3] = l.clearing;
            }
            f.lightInfo[0] = float(particleLights_.size());
            f.lightInfo[1] = particleLighting_.strength;
        }
        if (shadow) { // the traced light, on the same draws
            f.lightTerms[0] = sunSoft() ? 1.0f : 0.0f;
            f.lightTerms[1] = occluding() ? std::min(occlusion_.strength, 1.0f) : 0.0f;
            f.lightTerms[2] = std::clamp(sun_.tint, 0.0f, 1.0f);
        }
        f.lightTerms[3] = frameGlow_;
        f.lightTrace[0] = sunSoft() ? std::tan(std::min(sun_.softness, 20.0f) * 3.14159265f / 180.0f) : -1.0f;
        f.lightTrace[1] = occlusion_.radius;
        f.lightTrace[2] = occluding() ? float(std::clamp(occlusion_.rays, 1, 8)) : 0.0f;
        // a temporal upscaler averages the frames: the pattern turns each one, and what the blur leaves at an edge
        // turns into noise it resolves
        f.lightTrace[3] = up_ && up_->temporal() ? float(frameIndex_ % 64) * 2.39996323f : 0.0f;
        if (toon_.shading) {
            f.toon[0] = std::clamp(toon_.shade, 0.0f, 1.0f);
            f.toon[1] = std::max(toon_.rim, 0.0f);
            f.toon[2] = 1.0f;
            f.toon[3] = std::max(toon_.highlight, 0.0f);
        }
        f.outline[0] = toon_.width * float(t_.renderH) / 720.0f;
        f.outline[1] = std::clamp(toon_.strength, 0.0f, 1.0f);
        f.outline[2] = toon_.scenery ? 1.0f : 0.0f;
        f.outline[3] = std::max(toon_.reach, 1.0f);
        f.outlineLook[0] = std::clamp(toon_.ownColour, 0.0f, 1.0f);
        if (waveRemaster_ && wave_[0] > 0.0f) { // the sky (no fog) is replaced by the water seen far away
            f.water[0] = 1.0f;
            f.water[1] = waveSeconds_;
            f.water[2] = fog ? 0.0f : 1.0f;
            f.water[3] = waveHeat_ ? (heatOpenSky() ? 2.0f : 1.0f) : 0.0f;
            if (heatOpenSky()) { // the haze takes the sky's colour (the background), a little warmer
                for (int c = 0; c < 3; ++c) f.waterTint[c] = clear_[c];
                f.waterTint[3] = 1.0f;
            } else
                for (int c = 0; c < 3; ++c) f.waterTint[c] = waveTint_[c];
        }
        std::copy(std::begin(heightBounds_), std::end(heightBounds_), f.heightBounds);
        std::copy(std::begin(heightRange_), std::end(heightRange_), f.heightRange);
        f.precip[0] = precip_.snow;
        f.precip[1] = precip_.blizzard;
        f.precip[2] = precip_.gust;
        f.precip[3] = precip_.seconds;
        for (int c = 0; c < 3; ++c) f.precipWind[c] = precip_.wind[c];
        f.precipWind[3] = precip_.unit;
        f.precipView[0] = tanHalfFov_;
        f.precipView[1] = precipLight();
        XMStoreFloat4x4(&f.invViewProj, invVp_);
        f.softDepth[0] = nearZ_;
        f.softDepth[1] = kFar;
        f.softDepth[2] = effectNearFade_[0];
        f.softDepth[3] = effectNearFade_[1];
        const float look[8] = {precip_.density, precip_.size, precip_.brightness, precip_.swirl,
                               precip_.flutter, precip_.reach, precip_.streak, precip_.chaos};
        std::copy(std::begin(look), std::end(look), f.precipLook);
        std::copy(std::begin(flakeLayer_), std::end(flakeLayer_), f.precipLayer);
        cl_->writeBuffer(frameCb_, &f, sizeof f);
    }

    void drawScene(const Camera& cam) {
        const XMVECTOR eye = XMVectorSet(cam.eye[0], cam.eye[1], cam.eye[2], 1.0f);
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        const XMMATRIX view = XMMatrixLookToLH(eye, fwd, cameraUp(cam));
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(cam.fovY, float(t_.renderW) / float(t_.renderH), cam.nearZ, kFar);
        const XMMATRIX vp = view * proj;
        const XMMATRIX vpJit = view * withJitter(proj, jitter_.x, jitter_.y, t_.renderW, t_.renderH);
        // dome: camera rotation only (it follows the eye), current and previous
        const XMMATRIX rotView = XMMatrixLookToLH(XMVectorZero(), fwd, cameraUp(cam));
        if (!historyValid_) {
            prevVp_ = vp;
            prevRotVp_ = rotView * proj;
            for (int c = 0; c < 3; ++c) prevEye_[c] = cam.eye[c];
            historyValid_ = true;
        }
        float fwdv[3];
        for (int c = 0; c < 3; ++c) fwdv[c] = XMVectorGetByIndex(fwd, c);
        seeClock_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - seeStart_).count();
        // the see-through's holes: each body's box through the unjittered camera, its screen bounds widened by
        // kSeeHoleMargin (NDC) and its nearest view depth; a body with a corner behind the eye opens no hole
        constexpr float kSeeHoleMargin = 1.25f;
        for (size_t k = 0; k < kMaxSeeThroughBodies; ++k) {
            seeDepth_[k] = 0.0f;
            for (float& v : seeBody_[k]) v = 0.0f;
            if (k >= seeBodies_.size()) continue;
            const SeeThroughBody& b = seeBodies_[k];
            float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f}, nearest = 1e9f;
            bool behind = false;
            for (int corner = 0; corner < 8; ++corner) {
                const XMVECTOR p = XMVectorSet(corner & 1 ? b.hi[0] : b.lo[0], corner & 2 ? b.hi[1] : b.lo[1],
                                               corner & 4 ? b.hi[2] : b.lo[2], 1.0f);
                const float depth = XMVectorGetX(XMVector3Dot(XMVectorSubtract(p, eye), fwd));
                if (depth <= cam.nearZ) behind = true;
                nearest = std::min(nearest, depth);
                const XMVECTOR ndc = XMVector3TransformCoord(p, vp);
                for (int c = 0; c < 2; ++c) {
                    lo[c] = std::min(lo[c], XMVectorGetByIndex(ndc, c));
                    hi[c] = std::max(hi[c], XMVectorGetByIndex(ndc, c));
                }
            }
            if (behind) continue;
            seeBody_[k][0] = (lo[0] + hi[0]) * 0.5f;
            seeBody_[k][1] = (lo[1] + hi[1]) * 0.5f;
            seeBody_[k][2] = (hi[0] - lo[0]) * 0.5f * kSeeHoleMargin;
            seeBody_[k][3] = (hi[1] - lo[1]) * 0.5f * kSeeHoleMargin;
            seeDepth_[k] = nearest;
        }

        // streamed geometry: sky (dome previous positions follow the eye) and dynamic actors
        std::vector<float> skyPrev(sky_.size() * 3);
        for (size_t i = 0; i < sky_.size(); ++i)
            for (int c = 0; c < 3; ++c) skyPrev[i * 3 + c] = sky_[i].pos[c] - cam.eye[c];
        std::vector<GpuVertex> skyGpu;
        toGpu(sky_, skyPrev.data(), skyGpu);
        stream(skyBuf_, skyGpu, "sky vertices");
        stream(dynBuf_, dynGpu_, "dynamic vertices", rt_);
        if (dynCount_ && !dynTris_.empty()) expandDynamic();
        if (rt_) buildScene(std::min(dynStart_[0], dynCount_));

        if (depthCopyStale()) makeDepthCopy(); // (the set below binds it)
        invVp_ = XMMatrixInverse(nullptr, vpJit); // the traced light's passes and the blown snow's: NDC -> world
        if (sunSoft() || occluding())
            drawLight([&] { writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true); });
        nvrhi::GraphicsState gs;
        gs.framebuffer = t_.scene;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {sceneSet_}; // after buildScene: a grown attribute buffer makes a new set
        auto drawRange = [&](nvrhi::IGraphicsPipeline* p, nvrhi::IBuffer* vb, uint32_t first, uint32_t count) {
            if (!count) return;
            gs.pipeline = p;
            gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vb).setSlot(0).setOffset(0)};
            cl_->setGraphicsState(gs);
            cl_->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
        };

        if (!sky_.empty() && !skyScreen_) { // sky dome: world positions around the eye, no room fog
            // previous position = eye-relative (skyPrev), reprojected with the previous rotation-only matrix
            const float zero[3] = {0.0f, 0.0f, 0.0f};
            // reactive 1: scrolling cloud layers move without per-vertex history (motion covers rotation only)
            frameSky_ = 1.0f;
            writeFrame(vpJit, vp, prevRotVp_, cam.eye, zero, fwdv, cam.cullSign, false, 1.0f, 0.0f);
            drawRange(sky_pipe_, skyBuf_, 0, uint32_t(sky_.size()));
        } else if (!sky_.empty()) { // screen-space sky: identity transform, no depth, no room fog, no motion
            const float skyEye[3] = {0.0f, 0.0f, -1.0f}; // behind the screen plane: no layer push
            const float skyFwd[3] = {0.0f, 0.0f, 1.0f};
            skyRay_ = XMMatrixInverse(nullptr, rotView * proj);
            frameSky_ = 2.0f;
            writeFrame(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity(), skyEye, skyEye, skyFwd, 1.0f,
                       false, 1.0f, 1.0f);
            drawRange(sky_pipe_, skyBuf_, 0, uint32_t(sky_.size()));
        }
        frameSky_ = 0.0f;

        // every opaque surface (room, then actors / switchable segments) before any semi-transparent one: the blended
        // faces write no depth, so an opaque draw after them overwrote them wherever it lay behind (the ST4B sub 1
        // key case of static segment 9 vanished over the key and the switchable walls behind it); on the PS1 a
        // semi-transparent face always blends over what is behind it
        writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true);
        drawRange(opaque_, vbuf_, 0, semiStart_[0]);
        if (dynCount_) drawRange(opaque_, dynDrawn(), 0, std::min(dynStart_[0], dynCount_));
        // a semi-transparent polygon blends only the texels whose colour has the STP bit (Vram::decode): its other
        // texels are opaque surfaces, drawn here with the rest of the opaque ones (ST10's ice over the buried ship:
        // its snow is opaque, only the clear patches show the pit below)
        frameSemiPass_ = 1.0f;
        writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true);
        drawRange(opaque_, vbuf_, semiStart_[0], semiStart_[3] - semiStart_[0]);
        if (dynStart_[0] < dynCount_) drawRange(opaque_, dynDrawn(), dynStart_[0], dynCount_ - dynStart_[0]);
        // the opaque surfaces' depth, which the blended effects and the flakes fade against (scene.hlsl softEdge)
        copyDepth();
        if (toon_.outline && toon_.width > 0.0f && toon_.strength > 0.0f) { // under the blended draws
            writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f);
            drawOutline();
        }
        frameSemiPass_ = 2.0f;
        static const float kReactive[4] = {0.5f, 0.9f, 0.5f, 0.6f};      // avg, add, B*(1-F), B+F/4
        for (int mode = 0; mode < 3; ++mode) { // the room's semi quads (modes 0..2)
            const uint32_t first = semiStart_[mode], last = semiStart_[mode + 1];
            if (last <= first) continue;
            frameBlend_ = float(mode + 1);
            writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, kReactive[mode], 0.0f);
            drawRange(semi_[mode], vbuf_, first, last - first);
        }
        if (dynCount_) {
            for (int mode = 0; mode < 4; ++mode) { // blended effects (DynamicObject::blend = mode + 1)
                const uint32_t first = dynStart_[mode], last = std::min(dynStart_[mode + 1], dynCount_);
                if (last <= first) continue;
                frameBlend_ = float(mode + 1);
                // the light the additive ones add (B + F, B + F/4) is the bloom's source
                frameGlow_ = bloom_.enabled && bloom_.strength > 0.0f && (mode == 1 || mode == 3) ? 1.0f : 0.0f;
                if (frameGlow_ > 0.0f) glowDrawn_ = true;
                writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, kReactive[mode], 0.0f);
                drawRange(semi_[mode], dynDrawn(), first, last - first);
            }
        }
        frameGlow_ = 0.0f;
        frameBlend_ = 0.0f;
        frameSemiPass_ = 0.0f;
        // the see-through's open pieces: the opaque draws skipped them where they open; drawn again over everything as
        // a lens (scene.hlsl) over a copy of the scene so far (what lies behind them), behind what is nearer (depth
        // test, no depth write)
        if (!seePieces_.empty() && !seeDebug_) {
            constexpr float kLensReactive = 0.6f;
            if (t_.samples > 1) cl_->resolveTexture(sceneCopy_, nvrhi::AllSubresources, t_.msColor, nvrhi::AllSubresources);
            else cl_->copyTexture(sceneCopy_, nvrhi::TextureSlice(), t_.color, nvrhi::TextureSlice());
            frameLens_ = 1.0f;
            writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, kLensReactive, 0.0f);
            drawRange(semi_[0], vbuf_, 0, semiStart_[0]);
            if (dynCount_) drawRange(semi_[0], dynDrawn(), 0, std::min(dynStart_[0], dynCount_));
            frameLens_ = 0.0f;
        }

        // the precipitation's flakes, among the scene (the blown snow's medium follows after the resolve, draw())
        lastVpJit_ = vpJit;
        lastVp_ = vp;
        for (int c = 0; c < 3; ++c) {
            lastEye_[c] = cam.eye[c];
            lastFwd_[c] = fwdv[c];
        }
        if (precipitating()) {
            if (!flakeSet_) flakeSet_ = precipSet();
            nvrhi::GraphicsState fs;
            fs.pipeline = flakePipe_;
            fs.framebuffer = t_.scene;
            fs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
            fs.bindings = {flakeSet_};
            for (const bool farField : {false, true}) { // the near field, then the far one beyond it
                const float perCell = float(farField ? kFlakesFar : kFlakesNear);
                const float layer[4] = {farField ? kFlakeFarScale : 1.0f, perCell, farField ? 1.0f : 0.0f, 0.0f};
                std::copy(std::begin(layer), std::end(layer), flakeLayer_);
                writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f);
                cl_->setGraphicsState(fs);
                cl_->draw(nvrhi::DrawArguments().setVertexCount(kFlakeCellCount * uint32_t(perCell) * 6));
            }
        }

        prevVp_ = vp;
        prevRotVp_ = rotView * proj;
        for (int c = 0; c < 3; ++c) prevEye_[c] = cam.eye[c];
    }

    void drawOutput(nvrhi::ITexture* src, nvrhi::ITexture* back) {
        nvrhi::ITexture* depth = depthFresh_ ? depthCopy_.Get() : copyDepth(); // the underwater distance blur
        nvrhi::IFramebuffer* fb = backFramebuffer(back);
        auto it = outSets_.find(src);
        if (it == outSets_.end()) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(OutConsts)), nvrhi::BindingSetItem::Texture_SRV(0, src),
                           nvrhi::BindingSetItem::Sampler(0, pointClamp_), nvrhi::BindingSetItem::Sampler(1, linearClamp_),
                           nvrhi::BindingSetItem::Texture_SRV(1, depth), nvrhi::BindingSetItem::ConstantBuffer(1, lensCb_)};
            it = outSets_.emplace(src, dev_.device()->createBindingSet(bs, outLayout_)).first;
        }
        writeLensRings();
        nvrhi::GraphicsState gs;
        gs.pipeline = outPipe_;
        gs.framebuffer = fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(dev_.width()), float(dev_.height())));
        gs.bindings = {it->second};
        cl_->setGraphicsState(gs);
        OutConsts c{}; // the screen ripples in output pixels (PS1 320 x 240 fitted to the height, centred)
        const float w = float(dev_.width()), h = float(dev_.height()), s = h / 240.0f, ox = (w - 320.0f * s) * 0.5f;
        c.size[0] = w;
        c.size[1] = h;
        c.wave[0] = wave_[0] * s; // amplitude in output pixels
        c.wave[1] = wave_[1];     // phase in PS1 rows
        c.wave[2] = s;            // output pixels per PS1 row
        c.wave[3] = waveRemaster_ ? (waveHeat_ ? (heatOpenSky() ? 3.0f : 2.0f) : 1.0f) : 0.0f;
        if (waveRemaster_) { // the remaster look has no row phase / scale: the camera's pitch and tan(fov / 2) instead
            c.wave[1] = camPitch_;
            c.wave[2] = tanHalfFov_;
        }
        c.seconds = waveSeconds_;
        for (int k = 0; k < 3; ++k) c.tint[k] = wave_[0] > 0.0f ? waveTint_[k] : 1.0f;
        c.tint[3] = nearZ_; // the depth copy's near plane (far = kFar)
        for (const ScreenRipple& r : ripples_) {
            if (c.count >= kMaxScreenRipples) break;
            float* q = c.ring[c.count];
            q[0] = ox + r.cx * s;
            q[1] = r.cy * s;
            q[2] = r.r0 * s;
            q[3] = r.r1 * s;
            c.shift[c.count] = r.shift * s;
            ++c.count;
        }
        cl_->setPushConstants(&c, sizeof(c));
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    // the lens rings in output pixels: each centre through the drawn camera (lastVp_; w = the view depth), the radii
    // in the view plane scaled by the projection (a point straight above in view space, as exe 0x42e4a0 projects
    // them); a ring behind the near plane is left out
    void writeLensRings() {
        LensConsts l{};
        const float w = float(dev_.width()), h = float(dev_.height());
        for (const LensRing& r : lensRings_) {
            if (l.count >= kMaxLensRings) break;
            const XMVECTOR p = XMVector4Transform(XMVectorSet(r.pos[0], r.pos[1], r.pos[2], 1.0f), lastVp_);
            const float cw = XMVectorGetW(p);
            if (cw <= nearZ_) continue;
            const float pixelsPerUnit = h * 0.5f / (cw * tanHalfFov_);
            float* q = l.ring[l.count];
            q[0] = (XMVectorGetX(p) / cw * 0.5f + 0.5f) * w;
            q[1] = (0.5f - XMVectorGetY(p) / cw * 0.5f) * h;
            q[2] = r.inner * pixelsPerUnit;
            q[3] = r.width * pixelsPerUnit;
            l.kind[l.count][0] = r.outward ? 1.0f : 0.0f;
            ++l.count;
        }
        for (const ScreenTint& t : tints_) {
            if (l.tintCount >= kMaxScreenTints) break;
            float* q = l.tint[l.tintCount++];
            for (int c = 0; c < 3; ++c) q[c] = t.rgb[c];
            q[3] = t.semi ? float(t.abr) : -1.0f;
        }
        cl_->writeBuffer(lensCb_, &l, sizeof l);
    }

    // RGBA8 rows, tightly packed, top row first (what --shot saves)
    void readBack(nvrhi::IStagingTexture* staging, std::vector<uint8_t>& out) {
        dev_.device()->waitForIdle();
        size_t pitch = 0;
        const auto* src = static_cast<const uint8_t*>(
            dev_.device()->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        if (!src) return;
        const size_t w = size_t(dev_.width()), h = size_t(dev_.height());
        out.resize(w * h * 4);
        for (size_t y = 0; y < h; ++y) std::memcpy(out.data() + y * w * 4, src + y * pitch, w * 4);
        dev_.device()->unmapStagingTexture(staging);
        if (staging->getDesc().format == nvrhi::Format::BGRA8_UNORM) // a Vulkan swapchain's: out is RGBA
            for (size_t i = 0; i < out.size(); i += 4) std::swap(out[i], out[i + 2]);
    }

#ifdef _WIN32
    DeviceD3D12 dev_;
#else
    DeviceVulkan dev_;
#endif
    UiPass ui_;         // the 2D pass: the game canvas + the UI textures
    ImGuiNvrhi imgui_;  // the F1 panels, on ui_
    nvrhi::IFramebuffer* backFb_ = nullptr; // set while the overlay callback runs
    nvrhi::TextureHandle grabTex_; // sceneGrab
    uint64_t grabId_ = 0;
    int grabX_ = 0, grabY_ = 0, grabW_ = 0, grabH_ = 0;
    bool grabPending_ = false;
    bool vsync_ = true;
    nvrhi::CommandListHandle cl_;
    nvrhi::BufferHandle frameCb_, vbuf_, skyBuf_, dynBuf_;
    nvrhi::SamplerHandle pointClamp_, linearClamp_, anisoClamp_;
    nvrhi::ShaderHandle sceneVs_, scenePs_, outVs_, outPs_;
    nvrhi::InputLayoutHandle inputLayout_;
    struct OutConsts { // output.hlsl's push constants (128 bytes, HLSL cbuffer packing)
        float ring[kMaxScreenRipples][4]; // centre x / y, inner / outer radius (output pixels)
        float shift[4];                   // the picture's shift inside ring k (output pixels)
        int count;
        float size[2];
        float seconds; // the remaster underwater look's clock
        float wave[4]; // the flooded rooms' screen wave: amplitude (output pixels), phase (PS1 rows), pixels per row,
                       // style (0 PS1, 1 remaster)
        float tint[4]; // its colour / 0x80
    };
    float wave_[2] = {0.0f, 0.0f}, waveTint_[3] = {1.0f, 1.0f, 1.0f}, waveSeconds_ = 0.0f;
    bool waveRemaster_ = false, waveHeat_ = false;
    bool depthFresh_ = false; // depthCopy_ holds this frame's depth
    float nearZ_ = 8.0f, camPitch_ = 0.0f, tanHalfFov_ = 0.5f;
    std::vector<ScreenRipple> ripples_;
    struct LensConsts { // output.hlsl's Fx buffer (b1)
        float ring[kMaxLensRings][4]; // centre x / y, inner radius, width (output pixels)
        float kind[kMaxLensRings][4]; // x: 1 = role bit 0
        float tint[kMaxScreenTints][4]; // rgb, w: -1 opaque, else abr
        int count;
        int tintCount;
        int pad[2];
    };
    std::vector<LensRing> lensRings_;
    std::vector<ScreenTint> tints_;
    nvrhi::BufferHandle lensCb_;
    nvrhi::BindingLayoutHandle sceneLayout_, outLayout_;
    nvrhi::BindingSetHandle sceneSet_;
    nvrhi::GraphicsPipelineHandle opaque_, sky_pipe_, semi_[4], outPipe_;
    nvrhi::ShaderHandle debugVs_, debugPs_, debugMsPs_;
    nvrhi::ShaderHandle previewVs_, previewPs_; // renderPreview
    nvrhi::InputLayoutHandle previewInput_;
    nvrhi::BindingLayoutHandle previewLayout_;
    nvrhi::GraphicsPipelineHandle previewPipe_;
    PreviewSlot preview_[4];
    nvrhi::BindingLayoutHandle debugLayout_;
    std::unordered_map<int, nvrhi::GraphicsPipelineHandle> debugPipes_; // per target format, x2 + multisampled source
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> debugSets_;
    nvrhi::TextureHandle depthCopy_;
    nvrhi::TextureHandle sceneCopy_; // the scene's colour before the see-through's lens (render size, resolved)
    nvrhi::FramebufferHandle depthCopyFb_;
    DebugView view_ = DebugView::Final;
    nvrhi::TextureHandle pages_;
    int sliceSize_ = 256;
    std::map<int, std::vector<uint8_t>> sliceUpdates_;
    // the page pool (showPage / poolPage): pool_'s layers by page name, poolOrder_ most recently shown first
    static constexpr size_t kPoolBytes = size_t(1) << 30;
    static constexpr uint32_t kPoolFirstLayers = 64; // a room's pages fit: no growing, with its stall, on the first room
    nvrhi::TextureHandle pool_;
    int poolEdge_ = 0;          // pool_'s edge: the sliceSize it was made for
    uint32_t poolUsed_ = 0;     // layers handed out
    std::list<uint64_t> poolOrder_;
    std::unordered_map<uint64_t, std::pair<uint32_t, std::list<uint64_t>::iterator>> poolLayer_;
    std::map<uint32_t, std::vector<uint8_t>> poolWrites_; // next frame, before the copies
    std::map<int, uint32_t> poolShows_;                   // slice <- layer, next frame
    std::map<int, uint64_t> sliceWants_;                  // the page each slice is to show (showPage)
    std::vector<GpuVertex> staticGv_;                // the uploaded room vertices (updateStaticUvs patches them)
    uint32_t uvDirtyLo_ = UINT32_MAX, uvDirtyHi_ = 0; // their range not yet written to vbuf_
    Targets t_;
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> outSets_;
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> backFbs_;

    std::array<uint32_t, 4> semiStart_{};
    uint32_t vertexCount_ = 0;
    std::vector<RenderVertex> sky_, dyn_;
    std::vector<GpuVertex> dynGpu_;
    uint32_t dynCount_ = 0; // the vertices the dynamic draws read: dynGpu_'s, or what a subdivided frame makes of them
    std::array<uint32_t, 5> dynStart_{}; // dynamic vertices with blend <= m ([0] = end of the opaque objects)
    float frameSemiPass_ = 0.0f;         // FrameConstants::semiPass of the draws writeFrame is preparing
    float frameBlend_ = 0.0f;            // the blend of the draws writeFrame is preparing (FrameConstants::blendMode)
    float frameLens_ = 0.0f;           // FrameConstants::seeLens of the draws writeFrame is preparing
    float seeClock_ = 0.0f;              // the see-through lens's clock (seconds since the renderer started)
    std::chrono::steady_clock::time_point seeStart_ = std::chrono::steady_clock::now();
    std::vector<float> dynPrev_;
    MotionHistory history_;
    bool skyScreen_ = true;
    SkyPoles skyPoles_;      // the dome's poles (uploadSky)
    float effectNearFade_[2]{0.0f, 1.0f}; // setEffectNearFade: gone at [0], whole from [1]
    bool skyIsSky_ = true;   // the sky draw is the sky (not ST01's travel map): the remaster fog covers it
    float frameSky_ = 0.0f;  // FrameConstants::fogSky of the draws writeFrame is preparing
    bool historyValid_ = false;
    XMMATRIX prevVp_ = XMMatrixIdentity(), prevRotVp_ = XMMatrixIdentity(), skyRay_ = XMMatrixIdentity();
    float prevEye_[3] = {0.0f, 0.0f, 0.0f};
    Jitter jitter_{0.0f, 0.0f};
    std::unique_ptr<Upscaler> up_;
    AaBackend wanted_ = AaBackend::None;
    AaQuality quality_ = AaQuality::NativeAA;
    std::vector<AaBackend> available_;
    std::set<AaBackend> broken_; // backends that failed after init (feature creation / dispatch)
    uint64_t frameIndex_ = 0;
    bool pendingReset_ = true;
    std::chrono::steady_clock::time_point lastFrame_{};
    float clear_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Fog fog_;
    // sun shadows (ray queries)
    bool rt_ = false;
    SunLight sun_;
    // the traced light (drawLight): the sun's soft shadows, the ambient occlusion
    AmbientOcclusion occlusion_;
    nvrhi::ShaderHandle prePs_, lightPs_, lightBlurPs_[2];
    nvrhi::BindingLayoutHandle preLayout_, lightLayout_, lightBlurLayout_;
    nvrhi::BindingSetHandle preSet_, lightSet_[3];           // the prepass; the rays, the blur's two passes
    nvrhi::GraphicsPipelineHandle prePipe_, lightPipe_[3];
    nvrhi::TextureHandle preDepth_, preNormal_, preZ_;       // the prepass's view depth, normals and depth buffer
    nvrhi::TextureHandle lightPass_[3], light_;              // the rays' result, blurred along x, then y (= light_)
    nvrhi::TextureHandle lightNone_, preDepthNone_;          // bound while nothing is traced
    nvrhi::FramebufferHandle preFb_, lightFb_[3];
    // the anime look (scene.hlsl toon, ps_outline)
    ToonStyle toon_;
    nvrhi::ShaderHandle outlinePs_[2]; // the normal target as it is; multisampled (aa=msaa)
    nvrhi::BindingLayoutHandle outlineLayout_;
    nvrhi::BindingSetHandle outlineSet_;
    nvrhi::GraphicsPipelineHandle outlinePipe_;
    // the bloom (drawBloom)
    static constexpr float kBloomGain = 0.5f; // Bloom::strength 1: what the summed copies add of the glow
    Bloom bloom_;
    nvrhi::ShaderHandle bloomPs_[2]; // down, up
    nvrhi::BindingLayoutHandle bloomLayout_;
    nvrhi::BindingSetHandle bloomLast_;            // the largest copy, onto the colour
    nvrhi::GraphicsPipelineHandle bloomPipe_[3];   // down; up into a copy; up onto the colour
    float frameGlow_ = 0.0f;  // FrameConstants::lightTerms[3] of the draws writeFrame is preparing
    bool glowDrawn_ = false;  // this frame drew into the glow target
    ParticleLighting particleLighting_;
    ModelSubdivision subdivision_;
    struct ModelPlan {
        size_t count = 0;
        uint32_t layout = 0;
        SubdivisionPlan plan;
    };
    std::unordered_map<uint32_t, ModelPlan> modelPlans_; // by object id
    std::vector<float> planFaces_;
    // a subdivided frame (uploadDynamic, expandDynamic): 3 words per triangle of dynGpu_ (its first vertex, its first
    // vertex in dynFineBuf_, kTri*); empty: the frame is drawn as streamed
    std::vector<uint32_t> dynTris_;
    nvrhi::BufferHandle dynFineBuf_, dynTriBuf_;
    nvrhi::ShaderHandle subdivCs_;
    nvrhi::BindingLayoutHandle subdivLayout_;
    nvrhi::ComputePipelineHandle subdivPipe_;
    nvrhi::BindingSetHandle subdivSet_;
    nvrhi::IBuffer* subdivFrom_ = nullptr; // the dynBuf_ / dynAttr_ subdivSet_ binds
    nvrhi::IBuffer* subdivAttr_ = nullptr;
    std::vector<float> dynAttrCpu_; // buildScene's attributes of a frame drawn as streamed, kept for its capacity
    std::vector<ParticleLight> particleLights_; // this frame's (uploadDynamic)
    FrameCosts costs_;
    std::vector<SeeThroughBody> seeBodies_; // setSeeThrough
    std::vector<SeeThroughPiece> seePieces_;
    bool seeDebug_ = false;
    float seeBody_[kMaxSeeThroughBodies][4]{}; // drawScene: the bodies' holes on screen (FrameConstants seeBody)
    float seeDepth_[kMaxSeeThroughBodies]{};   // ... their nearest view depths (0: no hole)
    // precipitation (setPrecipitation): the room's height map from above, the flakes, the blown snow
    Precipitation precip_;
    nvrhi::ShaderHandle heightVs_, heightPs_, flakeVs_, flakePs_, fullVs_, blizzardPs_;
    nvrhi::BindingLayoutHandle precipLayout_;
    nvrhi::BindingSetHandle flakeSet_, blizzardSet_; // precipSet()
    nvrhi::GraphicsPipelineHandle heightPipe_, flakePipe_, blizzardPipe_;
    nvrhi::TextureHandle height_;
    float heightBounds_[4]{}, heightRange_[4]{0.0f, 1.0f, 1.0f, 1.0f};
    float flakeLayer_[4]{1.0f, 1.0f, 0.0f, 0.0f}; // FrameConstants::precipLayer of the flake draw being prepared
    // the last drawScene's camera (the blown snow's pass after the resolve), the inverse of its jittered viewProj
    XMMATRIX lastVpJit_ = XMMatrixIdentity(), lastVp_ = XMMatrixIdentity(), invVp_ = XMMatrixIdentity();
    float lastEye_[3]{}, lastFwd_[3]{0.0f, 0.0f, 1.0f};
    nvrhi::rt::AccelStructHandle tlas_, staticBlas_, dynBlas_;
    uint64_t dynBlasBytes_ = 0; // size of the buffer the actors' BLAS was made for,
    nvrhi::IBuffer* dynBlasFor_ = nullptr; // and the buffer (dynDrawn)
    nvrhi::BufferHandle staticAttr_, dynAttr_, indices_;
};

} // namespace

std::unique_ptr<Renderer> createRenderer(SDL_Window* window, int w, int h) {
    auto r = std::make_unique<NvrhiRenderer>();
#ifdef NDEBUG
    const bool debug = false;
#else
    const bool debug = true;
#endif
#ifdef _WIN32
    const auto hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!r->init(hwnd, w, h, debug)) return nullptr;
#else
    if (!r->init(window, w, h, debug)) return nullptr;
#endif
    return r;
}

} // namespace od
