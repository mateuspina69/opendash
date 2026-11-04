#include "app/sky.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <span>
#include <utility>

#include "engine/room.hpp"
#include "game/game_state.hpp"
#include "game/sky_gradient.hpp"
#include "game/sky_layers.hpp"
#include "game/travel.hpp"

namespace od::app {

void loadSky(RoomMesh& mesh, int stage, int sub) {
    const auto& definitions = od::port::roomSkyLayers(stage, sub);
    for (size_t i = 0; i < od::port::kSkyLayers; ++i) {
        const auto& d = definitions.layers[i];
        if (d.tilesAddress == 0) continue;
        SkyLayer L;
        L.slot = i;
        L.flags = uint8_t(d.flags);
        L.flags2 = uint8_t(d.flags >> 8);
        L.base = d.base;
        L.hofs = d.horizontalOffset;
        // The initializer seeds both current and previous scroll when mode != 0.
        if (L.flags2 != 0) {
            L.scrollX0 = d.base;
            L.scrollY0 = d.horizontalOffset;
        }
        L.speedX = d.speedX;
        L.w = int16_t(d.width);
        L.h = int16_t(d.height);
        L.clutWord = d.clut;
        L.tpage = uint16_t(d.tpage | uint16_t(d.drawMode >> 8) << 5);
        L.shade = float(d.colour & 0xff) / 128.0f;
        L.ot = uint8_t(d.colour >> 24);
        L.tiles = d.tiles;
        for (uint32_t word : L.tiles)
            if (word & 0xffff)
                L.sliceOfClut.try_emplace(uint16_t((word >> 16) + ((uint32_t(L.clutWord) * 0x4000) >> 16)), 0);
        if (i == 0 && (L.flags & od::port::kSkyLayerShown)) {
            mesh.skyGradHofs = L.hofs;
            mesh.skyGradW = L.w;
        }
        if (L.w > 0 && L.h > 0) mesh.sky.push_back(std::move(L));
    }
    std::stable_sort(mesh.sky.begin(), mesh.sky.end(), [](const SkyLayer& a, const SkyLayer& b) { return a.ot > b.ot; });
}

void loadSkyGradient(RoomMesh& mesh, int stage, int sub) {
    // the story picks the table in ST10 / ST11 / ST1D (game/sky_gradient)
    const auto gradient = od::port::roomSkyGradient(stage, sub, int(od::port::gameState().story));
    if (!gradient) return;
    if (gradient->cleared) mesh.bgCleared = true; // SetSkyGradient(0): isbg 1, the black clear (ST27 sub 0 ...)
    for (const std::array<uint32_t, 2>& band : gradient->bands) mesh.skyGrad.push_back({band[0], band[1]});
    if (!mesh.skyGrad.empty()) SDL_Log("ST%02X_%d: sky gradient, %zu bands", stage, sub, mesh.skyGrad.size());
}

namespace {

struct LayerScroll {
    uint8_t flags, mode;
    int16_t base, hofs, speedX, speedY;
    int32_t x, y;
    uint64_t ticks;
    bool live;
};

LayerScroll layerScroll(const RoomMesh& mesh, const SkyLayer& definition, uint64_t frame) {
    if (od::port::skyLayersInRoom(mesh.stage, mesh.sub)) {
        const auto layer = od::port::skyLayers()[definition.slot];
        return {layer.flags, layer.mode, layer.base, layer.horizontalOffset,
                layer.scrollSpeedX, layer.scrollSpeedY, layer.scrollX.whole, layer.scrollY.whole,
                od::port::skyLayerTicks(), true};
    }
    return {definition.flags, definition.flags2, definition.base, definition.hofs,
            definition.speedX, definition.speedY, definition.scrollX0, definition.scrollY0, frame, false};
}

void gradColor(uint32_t c, float* out) {
    for (int k = 0; k < 3; ++k) out[k] = float((c >> (8 * k)) & 0xff) / 255.0f;
}

// Maybe_DrawSkyGradient 0x5f03a0 on the virtual 240-line screen: 32-line gouraud bands (cmd 0x38, full width), band
// j at y = j * 32 - top where top = the gradient row at the screen top (hofs + 0x78 - horizon, as the layers)
void buildSkyGradient(const RoomMesh& mesh, int horizon, float vh, std::vector<od::RenderVertex>& out) {
    if (mesh.skyGrad.empty()) return;
    const int top = mesh.skyGradHofs + 0x78 - horizon;
    const int n = int(mesh.skyGrad.size());
    for (int j = std::max(0, top >> 5); j < n; ++j) {
        const float y0 = float(j * 32 - top), y1 = y0 + 32.0f;
        if (y0 >= vh) break;
        od::RenderVertex q[4];
        for (int k = 0; k < 4; ++k) {
            const int ix = k & 1, iy = k >> 1;
            q[k].pos[0] = float(ix) * 2.0f - 1.0f;
            q[k].pos[1] = 1.0f - (iy ? y1 : y0) / vh * 2.0f;
            q[k].pos[2] = 0.5f;
            q[k].uv[0] = q[k].uv[1] = 0.0f;
            q[k].slice = -1.0f; // untextured
            q[k].shade = 1.0f;
            q[k].cull = 0.0f;
            q[k].layer = 0.0f;
            gradColor(mesh.skyGrad[size_t(j)][size_t(iy)], q[k].color);
        }
        for (int idx : od::kQuadTris) out.push_back(q[idx]);
    }
}

constexpr float kHalfPi = 1.5707963f;
// the dome's fade toward a pole: from 62 degrees (1.082 rad), whole at 80 (1.396); the pole's colour is read half way
constexpr float kPoleFadeFrom = 1.0821f, kPoleFadeTo = 1.3963f, kPoleRing = 0.5f * (kPoleFadeFrom + kPoleFadeTo);

// a band of the sky sphere, untextured, from elevation el0 to el1 (clamped to the poles)
void appendSkyBand(const od::Camera& cam, float el0, float el1, const float* color0, const float* color1,
                   std::vector<od::RenderVertex>& out) {
    constexpr float kRadius = 160000.0f;
    constexpr int kSegs = 32;
    el0 = std::clamp(el0, -kHalfPi, kHalfPi);
    el1 = std::clamp(el1, -kHalfPi, kHalfPi);
    if (el0 == el1) return;
    for (int sgm = 0; sgm < kSegs; ++sgm) {
        od::RenderVertex q[4];
        for (int k = 0; k < 4; ++k) {
            const int ix = k & 1, iy = k >> 1;
            const float az = float(sgm + ix) * 6.2831853f / float(kSegs), el = iy ? el1 : el0;
            q[k].pos[0] = cam.eye[0] + kRadius * std::cos(el) * std::sin(az);
            q[k].pos[1] = cam.eye[1] + kRadius * std::sin(el);
            q[k].pos[2] = cam.eye[2] + kRadius * std::cos(el) * std::cos(az);
            q[k].uv[0] = q[k].uv[1] = 0.0f;
            q[k].slice = -1.0f;
            q[k].shade = 1.0f;
            q[k].cull = 0.0f;
            q[k].layer = 0.0f;
            for (int c = 0; c < 3; ++c) q[k].color[c] = iy ? color1[c] : color0[c];
        }
        for (int idx : od::kQuadTris) out.push_back(q[idx]);
    }
}

float gradientAngle(const RoomMesh& mesh) { return 6.2831853f / float(std::max(1, mesh.skyGradW) * 32); }

// The gradient on the sky sphere (as buildSkyDome maps the layers: elevation (hofs + 120 - row) * 2pi / (W * 32)), its
// first and last colours on to the poles. Layers in a room without one (ST1F sub 1) get its clear colour as a whole
// sphere: the poles' fade and the remaster fog need a surface wherever no layer has a tile, and the clear colour alone
// (no surface, no fog) would show as a seam against what is drawn
void buildSkyGradientDome(const RoomMesh& mesh, const od::Camera& cam, std::vector<od::RenderVertex>& out) {
    if (mesh.skyGrad.empty()) {
        const float black[3] = {0.0f, 0.0f, 0.0f};
        const float* clear = mesh.bgCleared ? black : mesh.clear;
        appendSkyBand(cam, kHalfPi, 0.0f, clear, clear, out);
        appendSkyBand(cam, 0.0f, -kHalfPi, clear, clear, out);
        return;
    }
    const float a = gradientAngle(mesh), horizonRow = float(mesh.skyGradHofs) + 120.0f;
    float first[3], last[3];
    gradColor(mesh.skyGrad.front()[0], first);
    gradColor(mesh.skyGrad.back()[1], last);
    appendSkyBand(cam, kHalfPi, horizonRow * a, first, first, out);
    for (size_t j = 0; j < mesh.skyGrad.size(); ++j) {
        float top[3], bottom[3];
        gradColor(mesh.skyGrad[j][0], top);
        gradColor(mesh.skyGrad[j][1], bottom);
        appendSkyBand(cam, (horizonRow - float(j * 32)) * a, (horizonRow - float(j * 32 + 32)) * a, top, bottom, out);
    }
    appendSkyBand(cam, (horizonRow - float(mesh.skyGrad.size() * 32)) * a, -kHalfPi, last, last, out);
}

// the sky behind the layers on a pole's ring (0 up, 1 down)
void poleBackdrop(const RoomMesh& mesh, int pole, float* out) {
    if (mesh.skyGrad.empty()) {
        for (int c = 0; c < 3; ++c) out[c] = mesh.bgCleared ? 0.0f : mesh.clear[c];
        return;
    }
    const float row = float(mesh.skyGradHofs) + 120.0f - (pole ? -kPoleRing : kPoleRing) / gradientAngle(mesh);
    const int n = int(mesh.skyGrad.size());
    const int j = std::clamp(int(std::floor(row / 32.0f)), 0, n - 1);
    const float t = std::clamp(row / 32.0f - float(j), 0.0f, 1.0f);
    float top[3], bottom[3];
    gradColor(mesh.skyGrad[size_t(j)][0], top);
    gradColor(mesh.skyGrad[size_t(j)][1], bottom);
    for (int c = 0; c < 3; ++c) out[c] = top[c] + (bottom[c] - top[c]) * t;
}

} // namespace

std::vector<od::RenderVertex> buildSky(const RoomMesh& mesh, const od::Camera& cam, uint64_t frame, float aspect) {
    std::vector<od::RenderVertex> out;
    const float vh = 240.0f, vw = vh * aspect, cx = vw * 0.5f;
    const float focal = (vh * 0.5f) / std::tan(cam.fovY * 0.5f);
    const int horizon = int(std::lround(vh * 0.5f + focal * std::tan(cam.pitch)));
    const int yaw = int(std::lround(cam.yaw * 4096.0f / 6.2831853f)) & 0xFFF;
    buildSkyGradient(mesh, horizon, vh, out); // drawn first (OT entry 0x8c: behind the layers)
    for (const SkyLayer& L : mesh.sky) {
        const LayerScroll scroll = layerScroll(mesh, L, frame);
        if (!(scroll.flags & od::port::kSkyLayerShown)) continue;
        const int tw = L.w * 32, th = L.h * 32;
        int x, y;
        if (scroll.mode & 1) { // manual scroll: speed * 0x100 per frame (16.16)
            x = scroll.x + (scroll.live ? 0 : int((int64_t(scroll.speedX) * int64_t(scroll.ticks)) >> 8));
            y = scroll.y + (scroll.live ? 0 : int((int64_t(scroll.speedY) * int64_t(scroll.ticks)) >> 8));
        } else {
            const int64_t v = (((int64_t(scroll.speedX) * int64_t(scroll.ticks)) >> 8) + (scroll.base + yaw) * 8) & 0x7FFF;
            x = int((v * L.w * 0x40) >> 16);
            // the game adds the projected reference point (FUN_005f2e60, its pitch convention is the
            // opposite of this camera's): content must move down when looking up
            y = scroll.hofs + 0x78 - horizon;
        }
        x -= int(std::lround((vw - 320.0f) * 0.5f)); // widen symmetrically around the original 320
        x = ((x % tw) + tw) % tw;
        y = ((y % th) + th) % th;
        const bool curved = (scroll.mode & 2) != 0;
        int row = (y >> 5) - 1, yRow = -32 - (y & 31);
        int rel = yRow + (y - scroll.hofs) - 0x78; // row relative to the horizon (camera height term dropped)
        if (row < 0) row += L.h;
        auto bend = [&](int r, float px) {
            if (!curved) return 0.0f;
            const float dx = px - cx;
            return float((int64_t(r) * int64_t(dx * dx) * 7) >> 21);
        };
        for (; yRow < int(vh); yRow += 32, rel += 32, row = (row + 1) % L.h) {
            int col = x >> 5;
            for (float px = float(-(x & 31)); px < vw; px += 32.0f, col = (col + 1) % L.w) {
                const uint32_t word = L.tiles[size_t(row) * size_t(L.w) + size_t(col)];
                if (!(word & 0xFFFF)) continue;
                const uint16_t clut = uint16_t((word >> 16) + ((uint32_t(L.clutWord) * 0x4000) >> 16));
                const auto it = L.sliceOfClut.find(clut);
                const float slice = it == L.sliceOfClut.end() ? 0.0f : float(it->second);
                const float u = float(word & 0xFF), v = float((word >> 8) & 0xFF);
                const float xs[2] = {px, px + 32.0f};
                od::RenderVertex q[4];
                for (int k = 0; k < 4; ++k) {
                    const int ix = k & 1, iy = k >> 1;
                    const float sy = float(yRow + 32 * iy) + bend(rel + 32 * iy, xs[ix]);
                    q[k].pos[0] = xs[ix] / vw * 2.0f - 1.0f;
                    q[k].pos[1] = 1.0f - sy / vh * 2.0f;
                    q[k].pos[2] = 0.5f;
                    q[k].uv[0] = (u + float(ix * 31) + 0.5f) / 256.0f;
                    q[k].uv[1] = (v + float(iy * 31) + 0.5f) / 256.0f;
                    q[k].slice = slice;
                    q[k].shade = L.shade;
                    q[k].cull = 0.0f;
                    q[k].layer = 0.0f;
                }
                for (int idx : od::kQuadTris) out.push_back(q[idx]);
            }
        }
    }
    return out;
}

void buildTravelMap(const RoomMesh& mesh, uint64_t frame, float aspect,
                    std::vector<od::RenderVertex>& out) {
    od::port::TravelMapDraw map;
    if (mesh.travelMapSlice[0] < 0 || !od::port::travelMapDraw(map)) return;
    const float vh = 240.0f, vw = vh * aspect, ox = (vw - 320.0f) * 0.5f;
    auto sprite = [&](float x, float y, float w, float h, float u, float v, int slice) {
        od::RenderVertex q[4];
        for (int k = 0; k < 4; ++k) {
            const int ix = k & 1, iy = k >> 1;
            const float sx = ox + x + w * float(ix), sy = y + h * float(iy);
            q[k].pos[0] = sx / vw * 2.0f - 1.0f;
            q[k].pos[1] = 1.0f - sy / vh * 2.0f;
            q[k].pos[2] = 0.5f;
            q[k].uv[0] = (u + float(ix) * (w - 1.0f) + 0.5f) / 256.0f;
            q[k].uv[1] = (v + float(iy) * (h - 1.0f) + 0.5f) / 256.0f;
            q[k].slice = float(slice);
            q[k].shade = 1.0f;
            q[k].cull = 0.0f;
            q[k].layer = 0.0f;
        }
        for (int idx : od::kQuadTris) out.push_back(q[idx]);
    };
    const int camX = map.cameraX, camZ = map.cameraZ;
    for (int i = 0; i < 4; ++i) { // FUN_00518c00
        const int x0 = ((i & 1) ? 0 : -0x100) + 0xa0 - camX;
        const int y0 = camZ - (i < 2 ? 0x100 : 0) + 0x78;
        for (int j = 0; j < 16; ++j) {
            const int u = (j & 3) * 64, v = (j >> 2) * 64;
            sprite(float(x0 + u), float(y0 + v), 64.0f, 64.0f, float(u), float(v), mesh.travelMapSlice[size_t(i)]);
        }
    }
    const float u = float(((frame >> 1) % 6) << 4); // FUN_00518a30
    for (const od::port::TravelMapIcon& e : map.destinations) {
        const int sx = e.mapX - camX, sz = camZ - e.mapZ;
        const int slice = mesh.travelMarkerSlice[e.index == map.storyMarker ? 0 : 1];
        sprite(float(sx + 0x98), float(sz + 0x60), 16.0f, 24.0f, u, 0.0f, slice);
        sprite(float(sx + 0x98), float(sz + 0x78), 16.0f, 16.0f, u, 24.0f, slice);
    }
}

void measureSkyPoles(RoomMesh& mesh) {
    constexpr size_t kPageBytes = 256 * 256 * 4;
    for (SkyLayer& L : mesh.sky) {
        const float a = 6.2831853f / float(L.w * 32);
        for (int pole = 0; pole < 2; ++pole) {
            // a panorama: the ring's row, if its art gets that far; a field that repeats: all of it
            int firstRow = 0, endRow = L.h;
            if (!(L.flags2 & 1)) {
                const float row = float(L.hofs) + 120.0f - (pole ? -kPoleRing : kPoleRing) / a;
                firstRow = int(std::floor(row / 32.0f));
                endRow = firstRow + 1;
                if (firstRow < 0 || firstRow >= L.h) continue;
            }
            uint64_t sum[3] = {0, 0, 0}, opaque = 0, texels = 0;
            for (int row = firstRow; row < endRow; ++row)
                for (int col = 0; col < L.w; ++col) {
                    texels += 32 * 32;
                    const uint32_t word = L.tiles[size_t(row) * size_t(L.w) + size_t(col)];
                    if (!(word & 0xFFFF)) continue;
                    const auto it = L.sliceOfClut.find(uint16_t((word >> 16) + ((uint32_t(L.clutWord) * 0x4000) >> 16)));
                    if (it == L.sliceOfClut.end() || (size_t(it->second) + 1) * kPageBytes > mesh.slices.size()) continue;
                    const uint8_t* page = mesh.slices.data() + size_t(it->second) * kPageBytes;
                    for (size_t y = 0; y < 32; ++y)
                        for (size_t x = 0; x < 32; ++x) {
                            const uint8_t* texel = page + (((((word >> 8) & 0xFF) + y) & 0xFF) * 256 + (((word & 0xFF) + x) & 0xFF)) * 4;
                            if (texel[3] < 64) continue; // as scene.hlsl clips it
                            for (int c = 0; c < 3; ++c) sum[c] += texel[c];
                            ++opaque;
                        }
                }
            if (!opaque) continue;
            for (int c = 0; c < 3; ++c)
                L.poleColor[pole][c] = std::min(1.0f, float(sum[c]) / float(opaque) / 255.0f * L.shade);
            L.poleCover[pole] = float(opaque) / float(texels);
        }
    }
}

std::vector<od::RenderVertex> buildSkyDome(const RoomMesh& mesh, const od::Camera& cam, uint64_t frame, od::SkyPoles& poles) {
    std::vector<od::RenderVertex> out;
    // no gradient and no layer shown (yet: a script's): no sky at all, only the clear colour
    const bool layerShown = std::any_of(mesh.sky.begin(), mesh.sky.end(), [&](const SkyLayer& L) {
        return (layerScroll(mesh, L, frame).flags & od::port::kSkyLayerShown) != 0;
    });
    if (mesh.skyGrad.empty() && !layerShown) return out;
    buildSkyGradientDome(mesh, cam, out);
    poleBackdrop(mesh, 0, poles.up);
    poleBackdrop(mesh, 1, poles.down);
    poles.fadeFrom = std::sin(kPoleFadeFrom);
    poles.fadeTo = std::sin(kPoleFadeTo);
    constexpr float kRadius = 150000.0f;
    for (const SkyLayer& L : mesh.sky) {
        const LayerScroll scroll = layerScroll(mesh, L, frame);
        if (!(scroll.flags & od::port::kSkyLayerShown)) continue;
        for (int c = 0; c < 3; ++c) { // the layers over each other, in draw order, as the rings show them
            poles.up[c] += (L.poleColor[0][c] - poles.up[c]) * L.poleCover[0];
            poles.down[c] += (L.poleColor[1][c] - poles.down[c]) * L.poleCover[1];
        }
        const float a = 6.2831853f / float(L.w * 32);
        float originPx; // panorama x that faces yaw 0
        if (scroll.mode & 1)
            originPx = float(scroll.x) + (scroll.live ? 0.0f : float((int64_t(scroll.speedX) * int64_t(scroll.ticks)) >> 8)) + 160.0f;
        else // buildSky's ((speed*ticks >> 8) + base*8) * W * 0x40 >> 16: only base carries the * 8, not the clock
            originPx = float(scroll.base) * float(L.w) / 128.0f +
                       float((((int64_t(scroll.speedX) * int64_t(scroll.ticks)) >> 8) * L.w) >> 10) + 160.0f;
        // manual scroll: the field the 2D draw wraps, its rows repeating up to the poles and following its scroll
        const bool field = (scroll.mode & 1) != 0;
        const int th = L.h * 32;
        const int rowScroll = field ? (((scroll.y + (scroll.live ? 0 : int((int64_t(scroll.speedY) * int64_t(scroll.ticks)) >> 8))) % th) + th) % th
                                    : scroll.hofs;
        const float horizonRow = float(rowScroll) + 120.0f;
        const int firstRow = field ? int(std::floor((horizonRow - kHalfPi / a) / 32.0f)) : 0;
        const int endRow = field ? int(std::ceil((horizonRow + kHalfPi / a) / 32.0f)) : int(L.h);
        for (int rowAt = firstRow; rowAt < endRow; ++rowAt) {
            const int row = ((rowAt % L.h) + L.h) % L.h;
            for (int col = 0; col < L.w; ++col) {
                const uint32_t word = L.tiles[size_t(row) * size_t(L.w) + size_t(col)];
                if (!(word & 0xFFFF)) continue;
                const uint16_t clut = uint16_t((word >> 16) + ((uint32_t(L.clutWord) * 0x4000) >> 16));
                const auto it = L.sliceOfClut.find(clut);
                const float slice = it == L.sliceOfClut.end() ? 0.0f : float(it->second);
                const float u = float(word & 0xFF), v = float((word >> 8) & 0xFF);
                od::RenderVertex q[4];
                for (int k = 0; k < 4; ++k) {
                    const int ix = k & 1, iy = k >> 1;
                    const float az = (float(col * 32 + ix * 32) - originPx) * a;
                    const float el = std::clamp((horizonRow - float(rowAt * 32 + iy * 32)) * a, -kHalfPi, kHalfPi);
                    q[k].pos[0] = cam.eye[0] + kRadius * std::cos(el) * std::sin(az);
                    q[k].pos[1] = cam.eye[1] + kRadius * std::sin(el);
                    q[k].pos[2] = cam.eye[2] + kRadius * std::cos(el) * std::cos(az);
                    q[k].uv[0] = (u + float(ix * 31) + 0.5f) / 256.0f;
                    q[k].uv[1] = (v + float(iy * 31) + 0.5f) / 256.0f;
                    q[k].slice = slice;
                    q[k].shade = L.shade;
                    q[k].cull = 0.0f;
                    q[k].layer = 0.0f;
                }
                for (int idx : od::kQuadTris) out.push_back(q[idx]);
            }
        }
    }
    return out;
}

void appendStormSky(const RoomMesh& mesh, const od::storm::Frame& storm, const od::Camera& cam, float aspect,
                    bool dome, std::vector<od::RenderVertex>& out) {
    if (storm.back.empty() || mesh.fxSlices.empty()) return;
    const float vh = 240.0f, vw = vh * aspect, ox = (vw - 320.0f) * 0.5f;
    const float f[3] = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};
    float r[3] = {f[2], 0.0f, -f[0]};
    const float rl = std::max(1e-6f, std::sqrt(r[0] * r[0] + r[2] * r[2]));
    r[0] /= rl;
    r[2] /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float th = std::tan(cam.fovY * 0.5f);
    constexpr float kRadius = 140000.0f;
    for (const od::storm::Sprite& s : storm.back) {
        if (s.texture < 0 || size_t(s.texture) >= mesh.fxSlices.size()) continue;
        const float slice = float(mesh.fxSlices[size_t(s.texture)]);
        od::RenderVertex q[4];
        for (int k = 0; k < 4; ++k) {
            const int ix = k & 1, iy = k >> 1;
            const float x = float(s.x + ix * s.w) + ox, y = float(s.y + iy * s.h);
            const float nx = x / vw * 2.0f - 1.0f, ny = 1.0f - y / vh * 2.0f;
            if (dome) {
                float d[3], len = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    d[c] = f[c] + r[c] * nx * th * aspect + u[c] * ny * th;
                    len += d[c] * d[c];
                }
                len = std::sqrt(len);
                for (int c = 0; c < 3; ++c) q[k].pos[c] = cam.eye[c] + d[c] / len * kRadius;
            } else {
                q[k].pos[0] = nx;
                q[k].pos[1] = ny;
                q[k].pos[2] = 0.5f;
            }
            q[k].uv[0] = (float(s.u) + float(ix * s.w)) / 256.0f;
            q[k].uv[1] = (float(s.v) + float(iy * s.h)) / 256.0f;
            q[k].slice = slice;
            q[k].shade = 1.0f;
            q[k].cull = 0.0f;
            q[k].layer = 0.0f;
        }
        for (int idx : od::kQuadTris) out.push_back(q[idx]);
    }
}

} // namespace od::app
