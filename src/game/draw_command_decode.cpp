// The draw pass over the draw commands and the OT packets (see draw_command_decode.hpp).
#include "game/draw_command_decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"

namespace od::dc {
namespace {

using port::fixedCos64;
using port::fixedSin64;

// the object ids of the decoded prims: kObjectBase | the actor's slot (its address from kObjectSlotBase, in words)
// << 4 | the record of that actor << 2 (the blend in bits 0..1); a particle's own record: kParticleObjectBase | a hash
constexpr uint32_t kObjectBase = 0x800000u, kObjectSlotBase = 0xa60000u, kObjectSlotMask = 0x7fffu;
constexpr uint32_t kParticleObjectBase = 0x400000u;
constexpr uint32_t kObjectRecordMask = 3u;
constexpr uint32_t kOtFt4Record = 3u, kOtGt4Record = 1u, kOtG4Record = 2u; // the OT packets' record numbers
// the screen ring's screen-point test (x -0x160 .. 0x2a0, y -0x88 .. 0x378)
constexpr uint16_t kScreenMarginX = 0x160, kScreenMarginY = 0x88, kScreenLimit = 0x401;
constexpr int32_t kScreenRingDepth = 0x80; // OpenDash: the view depth a screen ring is placed at
constexpr uint32_t kSegmentBias = 0x10;    // a ring's segment bytes count from this
constexpr float kTexelUnit = 128.0f, kColourUnit = 255.0f; // a textured / untextured colour's neutral value

// the actor that emitted the record at `rec` (the side channel's owner; 0: none recorded)
uint32_t ownerOf(uint32_t rec) {
    for (const auto& [a, o] : sideChannel().owners)
        if (a == rec) return o;
    return 0;
}
// the record's particle identity (the side channel's; -1: none, the record groups with its owner)
int32_t particleOf(uint32_t rec) {
    for (const auto& [a, t] : sideChannel().particles)
        if (a == rec) return t;
    return -1;
}

// a word's low half as an s16
int16_t s16of(uint32_t v) { return int16_t(uint16_t(v)); }
// the stage's coordinate shift (world units -> camera-input units)
int shiftOf() { return port::stage().coordinateShift & 0x1f; }
// the game camera's matrix element (row, column), 4096 = 1
int16_t cameraElement(int row, int column) { return port::cameraTransform().view.matrix.get().e[row * 3 + column]; }

// exe 0x5f11b0(record, out, view, shifted): the centre (word 2 x | y, word 4 z; << the stage shift when `shifted`)
// seen by the game camera: true = culled (depth outside 0x30 .. the effects' far * 4, or the screen point outside
// -0x160 .. 0x2a0 / -0x88 .. 0x378)
bool culled(const uint32_t* w, bool shifted, int16_t c[3]) {
    const int sh = shifted ? shiftOf() : 0;
    c[0] = int16_t(uint16_t(s16of(w[2]) << sh));
    c[1] = int16_t(uint16_t(s16of(w[2] >> 16) << sh));
    c[2] = int16_t(uint16_t(s16of(w[4]) << sh));
    // OpenDash: the exe culled the command for ITS camera (depth / screen of 0x6d0014); the picture is drawn from
    // the modern camera, so an effect outside the PS1 view vanished from the screen the player sees -- never culled
    // here, the GPU clips (draw output only: no logic reads this)
    return false;
}

// a vertex's colour multipliers from a 0xBBGGRR colour, each channel / unit (128 textured, 255 untextured)
void colour(fx::Vertex& v, uint32_t c, float unit) {
    for (int k = 0; k < 3; ++k) v.color[k] = float((c >> (8 * k)) & 0xff) / unit;
}

// a prim's centre in game units from a camera-input point (>> the stage shift sh)
void centreOf(fx::Prim& p, const int16_t c[3], int sh) {
    for (int k = 0; k < 3; ++k) p.centre[k] = float(c[k]) / float(1 << sh);
}

// 0x2c: corners C - (a, b), C + (-b, a), C + (b, -a), C + (a, b) in the camera plane, a / b = the 64-step cos / sin
// [(rot + 0x600) >> 6] * size (w6 & 0xffff, rot w6 >> 16); uv w3 / w5 / w7 / w9, CLUT w3 >> 16, tpage w5 >> 16,
// colour w1 (texel * colour / 128), bit 25 semi-transparent (the tpage's abr). The centre is NOT shifted (mode 0):
// in game units it is w / 2^shift.
void decode2c(const uint32_t* w, fx::TextureCache& tex, std::vector<fx::Prim>& out, uint32_t object) {
    int16_t c[3];
    if (culled(w, false, c)) return;
    const uint16_t clut = uint16_t(w[3] >> 16), tpage = uint16_t(w[5] >> 16);
    fx::Prim p;
    centreOf(p, c, shiftOf());
    p.object = object;
    p.texture = tex.index(tpage, clut);
    p.blend = ((w[1] >> 25) & 1) ? fx::blendOfAbr(tpage >> 5) : 0;
    const uint32_t i = ((uint32_t(int32_t(w[6]) >> 16) + 0x600) >> 6) & 0x3f;
    const float size = float(w[6] & 0xffff);
    const float a = float(fixedCos64(i)) * size / 4096.0f, b = float(fixedSin64(i)) * size / 4096.0f;
    const float off[4][2] = {{-a, -b}, {-b, a}, {b, -a}, {a, b}};
    const uint32_t uv[4] = {w[3], w[5], w[7], w[9]};
    for (int k = 0; k < 4; ++k) {
        p.v[k].off[0] = off[k][0];
        p.v[k].off[1] = off[k][1];
        p.v[k].u = float(uv[k] & 0xff) + 0.5f;
        p.v[k].v = float((uv[k] >> 8) & 0xff) + 0.5f;
        colour(p.v[k], w[1], kTexelUnit);
    }
    out.push_back(p);
}

// OpenDash: a ring's segments over the widest range its bytes allow (b0 / b1 = 0 .. 0xff), the ones outside
// start + 1 .. end - 1 collapsed onto the centre (radius 0, invisible): the ring keeps one vertex count while its arc
// grows / shrinks, so it blends between ticks and has motion vectors (the ST0E crate trail ring jittered at 30 Hz)
template <class F> void ringSegments(int s, int start, int end, F segment) {
    const int lo = ((0 - int(kSegmentBias)) >> s) + 1, hi = ((0xff - int(kSegmentBias)) >> s) + 1;
    for (int j = lo; j < hi; ++j) segment(j, j > start && j < end);
}

// 0xf0: G4 segments j = start + 1 .. end - 1 (start = (b0 - 0x10) >> s, end = ((b1 - 0x10) >> s) + 1; b0..b3 = the
// bytes of w0, s = (w4 >> 16) & 0xff) of the 64-step circle at angle (j << s) + b2, x scaled by (0x80 - b3) / 128
// and y by (0x80 + b3) / 128 (s8 b3: the ellipse); radius w5 & 0xffff with colour w1, radius w5 >> 16 with colour w3,
// in the camera plane; semi bit w3 bit 25, abr from the draw mode (w3 >> 24 & 0xf0) | 0xe1000200
void decodeF0(const uint32_t* w, std::vector<fx::Prim>& out, uint32_t object) {
    int16_t c[3];
    if (culled(w, true, c)) return;
    const int b0 = int(w[0] & 0xff), b1 = int((w[0] >> 8) & 0xff), b2 = int((w[0] >> 16) & 0xff);
    const int b3 = int(int8_t(w[0] >> 24));
    const int s = int((w[4] >> 16) & 0x1f);
    const int start = (b0 - int(kSegmentBias)) >> s, end = ((b1 - int(kSegmentBias)) >> s) + 1;
    const float rIn = float(w[5] & 0xffff), rOut = float(int32_t(w[5]) >> 16);
    const float sx = float(0x80 - b3) / 128.0f, sy = float(0x80 + b3) / 128.0f;
    const uint8_t blend = ((w[3] >> 25) & 1) ? fx::blendOfAbr(((w[3] >> 24) & 0xf0) >> 5) : 0;
    auto at = [&](int j, float r, uint32_t col) {
        const uint32_t a = uint32_t((j << s) + b2) & 0x3f;
        fx::Vertex v;
        v.off[0] = float(fixedCos64(a)) * sx * r / 4096.0f;
        v.off[1] = float(fixedSin64(a)) * sy * r / 4096.0f;
        colour(v, col, kColourUnit);
        return v;
    };
    ringSegments(s, start, end, [&](int j, bool shown) {
        fx::Prim p;
        centreOf(p, c, shiftOf());
        p.object = object;
        p.texture = fx::kUntextured;
        p.blend = blend;
        p.v[0] = at(j - 1, shown ? rIn : 0.0f, w[1]);
        p.v[1] = at(j, shown ? rIn : 0.0f, w[1]);
        p.v[2] = at(j - 1, shown ? rOut : 0.0f, w[3]);
        p.v[3] = at(j, shown ? rOut : 0.0f, w[3]);
        out.push_back(p);
    });
}

// 0xf1..0xf7 (0xf4: the exe's else branch of 0x5f07c5): the 0xf0 ring drawn around a SCREEN point -- word 2 = s16 x |
// s16 y in screen pixels (dropped outside x -0x160 .. 0x2a0 / y -0x88 .. 0x378), radii w5 in pixels, no projection;
// the segments / ellipse / colours / blend as 0xf0. OpenDash places it in the camera plane at the view depth
// kScreenRingDepth (the pixel point unprojected through the game camera 0x6d0014 / 0x6d0028, the projection distance
// and the screen centre) with the radii scaled to that depth, so it covers the same pixels.
void decodeF4(const uint32_t* w, std::vector<fx::Prim>& out, uint32_t object) {
    const int16_t sx = s16of(w[2]), sy = s16of(w[2] >> 16);
    if (uint16_t(sx + kScreenMarginX) >= kScreenLimit || uint16_t(sy + kScreenMarginY) >= kScreenLimit) return;
    const port::ProjectionState& proj = port::projection();
    const int32_t h = proj.focal, cx = proj.centreX, cy = proj.centreY;
    if (h == 0) return;
    const double view[3] = {double(sx - cx) * kScreenRingDepth / h, double(sy - cy) * kScreenRingDepth / h,
                            double(kScreenRingDepth)};
    double d[3];
    for (int k = 0; k < 3; ++k) d[k] = view[k] - double(port::cameraTransform().view.translation[size_t(k)]);
    int16_t c[3];
    for (int i = 0; i < 3; ++i) { // R^T (view - T), R = the camera's rows scaled 4096
        double v = 0;
        for (int j = 0; j < 3; ++j) v += double(cameraElement(j, i)) * d[j];
        c[i] = int16_t(std::clamp(v / 4096.0, -32768.0, 32767.0));
    }
    const int b0 = int(w[0] & 0xff), b1 = int((w[0] >> 8) & 0xff), b2 = int((w[0] >> 16) & 0xff);
    const int b3 = int(int8_t(w[0] >> 24));
    const int s = int((w[4] >> 16) & 0x1f);
    const int start = (b0 - int(kSegmentBias)) >> s, end = ((b1 - int(kSegmentBias)) >> s) + 1;
    const float scale = float(kScreenRingDepth) / float(h); // pixels -> camera-plane units at the depth
    const float rIn = float(w[5] & 0xffff) * scale, rOut = float(int32_t(w[5]) >> 16) * scale;
    const float ex = float(0x80 - b3) / 128.0f, ey = float(0x80 + b3) / 128.0f;
    const uint8_t blend = ((w[3] >> 25) & 1) ? fx::blendOfAbr(((w[3] >> 24) & 0xf0) >> 5) : 0;
    auto at = [&](int j, float r, uint32_t col) {
        const uint32_t a = uint32_t((j << s) + b2) & 0x3f;
        fx::Vertex v;
        v.off[0] = float(fixedCos64(a)) * ex * r / 4096.0f;
        v.off[1] = float(fixedSin64(a)) * ey * r / 4096.0f;
        colour(v, col, kColourUnit);
        return v;
    };
    ringSegments(s, start, end, [&](int j, bool shown) {
        fx::Prim p;
        centreOf(p, c, shiftOf());
        p.object = object;
        p.texture = fx::kUntextured;
        p.blend = blend;
        p.v[0] = at(j - 1, shown ? rIn : 0.0f, w[1]);
        p.v[1] = at(j, shown ? rIn : 0.0f, w[1]);
        p.v[2] = at(j - 1, shown ? rOut : 0.0f, w[3]);
        p.v[3] = at(j, shown ? rOut : 0.0f, w[3]);
        out.push_back(p);
    });
}

// 0xf8 (transcribed from 0x5f0580 case 0xf8): with the centre C (word 2 x | y, word 4 z, << shift), A = (b0 - 0x10)
// & 0x3f, B = (b1 - 0x10) & 0x3f (the tilts), k = 12 - shift:
//   inner ring: y = C.y + sin[A] * rIn >> k, horizontal radius RI = cos[A] * rIn >> 12 (rIn = w5 & 0xffff)
//   outer ring: y = C.y + sin[B] * rOut >> k, horizontal radius RO = cos[B] * rOut >> 12 (rOut = w5 >> 16)
//   point at angle t: (C.x + cos[t] * R >> k, y, C.z + sin[t] * R >> k) -- the world XZ plane (game Y down)
// one GT4 per step j = step + b2 .. b2 + 0x40 + step - 1 by step = 1 << s (s = (w4 >> 16) & 0xff): (outer(j - step),
// inner(j - step), outer(j), inner(j)), colours w3 (outer) / w1 (inner), semi bit w3 bit 25; texture CLUT w6 >> 16,
// tpage w7 >> 16; u = ((j >> s) & (2^b3 - 1)) * (d + 1) + u6 .. + d with d = (u7 - u6) >> b3, v = v7 outer / v6 inner
// (u6 / v6 = the low bytes of w6, u7 / v7 of w7). The exe draws it when w4 >> 24 != 0 (double sided) or one of the
// quad's two GTE NCLIPs says so; OpenDash draws every segment, both sides.
void decodeF8(const uint32_t* w, fx::TextureCache& tex, std::vector<fx::Prim>& out, uint32_t object) {
    int16_t c[3];
    if (culled(w, true, c)) return;
    const int sh = shiftOf(), k = (12 - sh) & 0x1f;
    const uint32_t w0 = w[0], w4 = w[4];
    const uint32_t A = (w0 - kSegmentBias) & 0x3f, B = uint32_t((int32_t(w0) >> 8) - int32_t(kSegmentBias)) & 0x3f;
    const uint32_t s = (w4 >> 16) & 0xff, b2 = uint32_t(int32_t(w0) >> 16) & 0x3f;
    const int step = 1 << (s & 0x1f);
    const int32_t rIn = int32_t(w[5] & 0xffff), rOut = int32_t(w[5]) >> 16;
    const int16_t yIn = int16_t((fixedSin64(A) * rIn >> k) + c[1]);
    const int16_t yOut = int16_t((fixedSin64(B) * rOut >> k) + c[1]);
    const int32_t RI = (fixedCos64(A) * rIn) >> 12, RO = (fixedCos64(B) * rOut) >> 12;
    auto point = [&](uint32_t t, int32_t R, int16_t y, int16_t p[3]) {
        p[0] = int16_t(int16_t(fixedCos64(t) * R >> k) + c[0]);
        p[1] = y;
        p[2] = int16_t(int16_t(fixedSin64(t) * R >> k) + c[2]);
    };
    const uint32_t u6 = w[6] & 0xff, v6 = (w[6] >> 8) & 0xff, u7 = w[7] & 0xff, v7 = (w[7] >> 8) & 0xff;
    const uint16_t clut = uint16_t(w[6] >> 16), tpage = uint16_t(w[7] >> 16);
    const uint32_t b3 = (w0 >> 24) & 0x1f;
    const int mask = (1 << b3) - 1;
    const int16_t d = int16_t((int32_t(u7) - int32_t(u6)) >> b3);
    const int texture = tex.index(tpage, clut);
    const uint8_t blend = ((w[3] >> 25) & 1) ? fx::blendOfAbr(tpage >> 5) : 0;
    int16_t inPrev[3], outPrev[3];
    point(b2, RI, yIn, inPrev);
    point(b2, RO, yOut, outPrev);
    const float unit = float(1 << sh);
    for (int j = step + int(b2); j < step + int(b2) + 0x40; j += step) {
        int16_t inNew[3], outNew[3];
        point(uint32_t(j) & 0x3f, RI, yIn, inNew);
        point(uint32_t(j) & 0x3f, RO, yOut, outNew);
        const int16_t u0 = int16_t(((j >> s) & mask) * (d + 1) + int(u6));
        fx::Prim p;
        centreOf(p, c, sh);
        p.screen = false;
        p.object = object;
        p.texture = texture;
        p.blend = blend;
        const int16_t* pts[4] = {outPrev, inPrev, outNew, inNew};
        const uint32_t us[4] = {uint32_t(u0) & 0xff, uint32_t(u0) & 0xff, uint32_t(u0 + d) & 0xff,
                                uint32_t(u0 + d) & 0xff};
        const uint32_t vs[4] = {v7, v6, v7, v6};
        for (int q = 0; q < 4; ++q) {
            for (int a = 0; a < 3; ++a) p.v[q].off[a] = float(pts[q][a] - c[a]) / unit;
            p.v[q].u = float(us[q]) + 0.5f;
            p.v[q].v = float(vs[q]) + 0.5f;
            colour(p.v[q], (q & 1) ? w[1] : w[3], kTexelUnit);
        }
        out.push_back(p);
        std::copy(inNew, inNew + 3, inPrev);
        std::copy(outNew, outNew + 3, outPrev);
    }
}

// one record: its size in words (0 = unknown opcode: the exe's walk stops there)
size_t one(const uint32_t* w, size_t avail, fx::TextureCache& tex, std::vector<fx::Prim>& out, uint32_t object,
           Stats& st) {
    const uint32_t code = w[1] >> 24;
    const uint32_t op = code & kRecordOpcodeMask;
    const size_t n = op == kBillboardRecord                          ? kBillboardWords
                     : (op == kRingRecord || op == kScreenRingRecord) ? kRingWords
                     : op == kBandRecord                             ? kBandWords
                                                                     : 0;
    if (n == 0 || n > avail) {
        ++st.unknown;
        return 0;
    }
    ++st.records;
    if (op == kBillboardRecord) {
        ++st.b2c;
        decode2c(w, tex, out, object);
    } else if (op == kRingRecord && code == kRingRecord) { // the exe tests the whole byte +7 for the projected variant
        ++st.f0;
        decodeF0(w, out, object);
    } else if (op == kBandRecord) {
        ++st.f8;
        decodeF8(w, tex, out, object);
    } else {
        ++st.f4; // 0xf4 (and 0xf1..0xf3 / 0xf5..0xf7): the screen-space disc
        decodeF4(w, out, object);
    }
    return n;
}

// the object id of an OT packet of `owner` (record number k)
uint32_t packetObject(uint32_t owner, uint32_t k) {
    return kObjectBase | ((((owner - kObjectSlotBase) >> 2) & kObjectSlotMask) << 4) | (k << 2);
}

} // namespace

Stats decode(fx::TextureCache& tex, std::vector<fx::Prim>& out) {
    Stats st;
    DrawCommandState& dcs = drawCommands();
    const uint32_t begin = buffer(), end = dcs.cursor;
    std::vector<uint32_t> w;
    // the frame drawing nothing: the draw pass skips the commands (not the OT)
    if (port::display().drawingOff == 0 && end > begin && DrawCommandState::holds(begin, (end - begin) / 4)) {
        w.resize((end - begin) / 4);
        for (size_t i = 0; i < w.size(); ++i) w[i] = dcs.word(begin + uint32_t(i) * 4);
    }
    std::vector<std::pair<uint32_t, int>> seen; // owner -> records so far (object ids per actor)
    for (size_t i = 0; i < w.size();) {
        const uint32_t rec = begin + uint32_t(i * 4), owner = ownerOf(rec);
        int k = 0;
        auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& e) { return e.first == owner; });
        if (it == seen.end()) seen.emplace_back(owner, 1);
        else k = it->second++;
        uint32_t object = packetObject(owner, uint32_t(k) & kObjectRecordMask);
        if (const int32_t part = particleOf(rec); part >= 0) { // its own object: owner x particle identity (hashed)
            const uint32_t h = ((owner - kObjectSlotBase) >> 2) * 2654435761u ^ (uint32_t(part) * 40503u + 0x9e37u);
            object = kParticleObjectBase | ((h >> 8) & 0xfffffu) << 2;
        }
        const size_t n = one(&w[i], w.size() - i, tex, out, object, st);
        if (n == 0) break;
        i += n;
    }
    const SideChannel& side = sideChannel();
    // the OT quads (POLY_FT4: code bit 1 semi-transparent through the tpage's abr, bit 0 raw texture)
    const int sh = shiftOf();
    const float unit = float(1 << sh);
    for (const OtQuadFT4Packet& q : side.ft4) {
        ++st.ot;
        fx::Prim p;
        centreOf(p, q.v[0], sh);
        p.screen = false;
        p.object = packetObject(q.owner, kOtFt4Record);
        const uint16_t clut = uint16_t(q.w[3] >> 16), tpage = uint16_t(q.w[5] >> 16);
        const uint32_t code = q.w[1] >> 24;
        p.texture = tex.index(tpage, clut);
        p.blend = (code & 2) ? fx::blendOfAbr(tpage >> 5) : 0;
        const uint32_t uv[4] = {q.w[3], q.w[5], q.w[7], q.w[9]};
        for (int k = 0; k < 4; ++k) {
            for (int a = 0; a < 3; ++a) p.v[k].off[a] = float(q.v[k][a] - q.v[0][a]) / unit;
            p.v[k].u = float(uv[k] & 0xff) + 0.5f;
            p.v[k].v = float((uv[k] >> 8) & 0xff) + 0.5f;
            if (!(code & 1)) colour(p.v[k], q.w[1], kTexelUnit);
        }
        out.push_back(p);
    }
    // the gouraud OT quads (POLY_G4 code bit 1: semi-transparent with the DR_MODE's abr); the camera rows give the
    // screen axes in the world (view = R * v + t, 4096 = 1)
    float row[2][3]{};
    for (int i = 0; i < 2; ++i)
        for (int k = 0; k < 3; ++k) row[i][k] = float(cameraElement(i, k)) / 4096.0f;
    for (const OtQuadG4Packet& q : side.g4) {
        ++st.otG4;
        fx::Prim p;
        centreOf(p, q.v[0], sh);
        p.screen = false;
        p.object = packetObject(q.owner, kOtG4Record);
        p.texture = fx::kUntextured;
        p.blend = ((q.c[0] >> 24) & 2) ? fx::blendOfAbr(q.abr) : 0;
        for (int k = 0; k < 4; ++k) {
            for (int a = 0; a < 3; ++a)
                p.v[k].off[a] =
                    (float(q.v[k][a] - q.v[0][a]) + row[0][a] * q.off[k][0] + row[1][a] * q.off[k][1]) / unit;
            colour(p.v[k], q.c[k], kColourUnit);
        }
        out.push_back(p);
    }
    // the gouraud OT lines (LINE_G2): a 1-PS1-pixel-wide quad. The ends (point + camera-plane offset) through the game
    // camera (view = R * p / 4096 + t; screen = the projection distance * view xy / view z): the screen perpendicular
    // n of the projected segment, each end widened by +- n * half a pixel at its depth (z / focal)
    if (!side.lines.empty()) {
        float rot[3][3], tr[3];
        for (int i = 0; i < 3; ++i) {
            for (int k = 0; k < 3; ++k) rot[i][k] = float(cameraElement(i, k)) / 4096.0f;
            tr[i] = float(port::cameraTransform().view.translation[size_t(i)]);
        }
        const float h = float(port::projection().focal);
        for (const OtLineG2Packet& q : side.lines) {
            float pt[2][3], z[2], sx[2], sy[2];
            bool ok = h > 0.0f;
            for (int e = 0; e < 2 && ok; ++e) {
                for (int a = 0; a < 3; ++a)
                    pt[e][a] = float(q.v[e][a]) + row[0][a] * q.off[e][0] + row[1][a] * q.off[e][1];
                float vw[3];
                for (int i = 0; i < 3; ++i)
                    vw[i] = rot[i][0] * pt[e][0] + rot[i][1] * pt[e][1] + rot[i][2] * pt[e][2] + tr[i];
                z[e] = vw[2];
                if (z[e] <= 0.0f) ok = false;
                else {
                    sx[e] = h * vw[0] / z[e];
                    sy[e] = h * vw[1] / z[e];
                }
            }
            if (!ok) continue; // an end behind the eye
            float nx = -(sy[1] - sy[0]), ny = sx[1] - sx[0];
            const float len = std::sqrt(nx * nx + ny * ny);
            if (len < 1e-4f) nx = 0.0f, ny = 1.0f; // one point: a 1-pixel tick
            else nx /= len, ny /= len;
            ++st.otG4;
            fx::Prim p;
            centreOf(p, q.v[0], sh);
            p.screen = false;
            p.object = packetObject(q.owner, kOtG4Record);
            p.texture = fx::kUntextured;
            p.blend = ((q.c[0] >> 24) & 2) ? fx::blendOfAbr(q.abr) : 0;
            // corners 0 / 1 = end 0 -/+, 2 / 3 = end 1 -/+ (triangles 0 1 2 / 1 3 2)
            for (int k = 0; k < 4; ++k) {
                const int e = k >> 1;
                const float s = ((k & 1) ? 0.5f : -0.5f) * z[e] / h;
                for (int a = 0; a < 3; ++a)
                    p.v[k].off[a] = (pt[e][a] + (row[0][a] * nx + row[1][a] * ny) * s - float(q.v[0][a])) / unit;
                colour(p.v[k], q.c[e], kColourUnit);
            }
            out.push_back(p);
        }
    }
    // the textured gouraud OT quads (POLY_GT4: as the FT4 above, a colour per corner, offsets as the G4)
    for (const OtQuadGT4Packet& q : side.gt4) {
        ++st.ot;
        fx::Prim p;
        centreOf(p, q.v[0], sh);
        p.screen = false;
        p.object = packetObject(q.owner, kOtGt4Record);
        const uint16_t clut = uint16_t(q.w[3] >> 16), tpage = uint16_t(q.w[6] >> 16);
        const uint32_t code = q.w[1] >> 24;
        p.texture = tex.index(tpage, clut);
        p.blend = (code & 2) ? fx::blendOfAbr(tpage >> 5) : 0;
        const uint32_t uv[4] = {q.w[3], q.w[6], q.w[9], q.w[12]}, col[4] = {q.w[1], q.w[4], q.w[7], q.w[10]};
        for (int k = 0; k < 4; ++k) {
            for (int a = 0; a < 3; ++a)
                p.v[k].off[a] =
                    (float(q.v[k][a] - q.v[0][a]) + row[0][a] * q.off[k][0] + row[1][a] * q.off[k][1]) / unit;
            p.v[k].u = float(uv[k] & 0xff) + 0.5f;
            p.v[k].v = float((uv[k] >> 8) & 0xff) + 0.5f;
            if (!(code & 1)) colour(p.v[k], col[k], kTexelUnit);
        }
        out.push_back(p);
    }
    return st;
}

Stats decodeRecords(const uint32_t* w, size_t words, fx::TextureCache& tex, std::vector<fx::Prim>& out,
                    uint32_t object) {
    Stats st;
    for (size_t i = 0; i < words;) {
        const size_t n = one(w + i, words - i, tex, out, object, st);
        if (n == 0) break;
        i += n;
    }
    return st;
}

} // namespace od::dc
