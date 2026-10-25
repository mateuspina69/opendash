// The crash-landing storm of script 6 as the game draws it: see crash_landing_draw.hpp.
#include "content/intro/crash_landing_draw.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "content/intro/crash_landing.hpp"
#include "content/intro/opening_effects.hpp"
#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/enemy_shot.hpp"
#include "game/matrix.hpp"
#include "game/projection.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"

namespace od::storm {
namespace {

// the classes drawn (their update routines)
constexpr RoutineId kCloudRingClass = RoutineId::CloudRingUpdate, kOpeningEffectClass = RoutineId::OpeningEffectUpdate,
                    kCrashFlashClass = RoutineId::CrashFlashUpdate;
constexpr RoutineId kFallingPieceClass = RoutineId::FallingPieceUpdate, kEnemyShotClass = RoutineId::EnemyShotUpdate,
                    kCrashCraftClass = RoutineId::CrashCraftUpdate;
// the prims' object ids: a base by class and the slot's index
constexpr uint32_t kCloudObjects = 0x70000u, kFlashObjects = 0x74000u, kProjectileObjects = 0x75000u,
                   kCraftObjects = 0x76000u;
constexpr uint16_t kBackdropTpage = 0x1b, kBackdropClut = 0x7c80; // the backdrop's draw mode / CLUT words
constexpr int kExtra = 160; // PS1 pixels drawn left / right of the 320-pixel frame (windows up to 2.33:1)
constexpr int kBackdropCell = 128, kBackdropCells = 8;
constexpr int32_t kCraftJetsTick = 0x16; // the script step tick the landing craft's jets light at

// ---- the cloud textures -----------------------------------------------------------------------------------------
// The cloud texture table (exe data 0x8ce350, 12-byte records by the ring's entry): the texture page's VRAM x / y, the
// second CLUT's x / y, the first CLUT's x / y
constexpr uint32_t kCloudEntrySize = 12, kCloudEntryCount = 8;
struct CloudEntryRecord : DataRecord {
    uint16_t pageX = 0, pageY = 0, altClutX = 0, altClutY = 0, clutX = 0, clutY = 0;
    static CloudEntryRecord load(const TableReader& r) {
        return {{}, r.u16(0), r.u16(2), r.u16(4), r.u16(6), r.u16(8), r.u16(0xa)};
    }
};
const DataTable<RecordTable<CloudEntryRecord, kCloudEntryCount, kCloudEntrySize>> cloudEntries{TableId::CloudTextures};
// the puff rings' tables (exe data 0x8ce45c.., by ring): the height offsets, the radii, the puffs' sizes, the CLUT x
// offsets; the puffs' u by puff & 7
constexpr uint32_t kPuffRings = 4, kPuffsPerRing = 32;
const DataTable<ScalarTable<int16_t, kPuffRings>> puffRingHeights{TableId::PuffRingHeights};
const DataTable<ScalarTable<int16_t, kPuffRings>> puffRingRadii{TableId::PuffRingRadii};
const DataTable<ScalarTable<int16_t, kPuffRings>> puffSizes{TableId::PuffSizes};
const DataTable<ScalarTable<uint8_t, 8>> puffU{TableId::PuffU};
const DataTable<ScalarTable<int8_t, kPuffRings>> puffRingClutX{TableId::PuffRingClutX};
// the panoramas' tile rows' u (exe data 0x8ce490 / 0x8ce49c: the sea panorama's 5 rows, the sky panorama's 10)
using PanoramaRows = ScalarTable<uint8_t, 10>;
const DataTable<PanoramaRows> seaPanoramaRows{TableId::SeaPanoramaRows};
const DataTable<PanoramaRows> skyPanoramaRows{TableId::SkyPanoramaRows};

// an entry as the draw functions decode it: the tpage of the draw mode (E1, 4 bit, abr 0; ((x >> 2) & 0xf0 | y & 0x100)
// >> 4 | (y & 0x200) << 2) and the CLUT word (x >> 4 & 0x3f | y << 6) with an x offset
struct Entry {
    uint16_t tpage = 0, clutX = 0, clutY = 0;
    uint8_t v0 = 0; // the low byte of the page y (the puffs' v0)
};
Entry decodeEntry(uint16_t x, uint16_t y, uint16_t clutX, uint16_t clutY) {
    Entry r;
    r.tpage = uint16_t((((x >> 2) & 0xf0) | (y & 0x100)) >> 4 | (y & 0x200) << 2);
    r.clutX = clutX;
    r.clutY = clutY;
    r.v0 = uint8_t(y);
    return r;
}
// entry e of the cloud texture table (alt: its second CLUT)
Entry entry(uint32_t e, bool alt) {
    const CloudEntryRecord& r = cloudEntries[e];
    return decodeEntry(r.pageX, r.pageY, alt ? r.altClutX : r.clutX, alt ? r.altClutY : r.clutY);
}
uint16_t clutWord(uint16_t x, uint16_t y) { return uint16_t(((x >> 4) & 0x3f) | (y << 6)); }

// what the storm's draws write into: the texture cache, the first storm gradient row, the 2D frame, the 3D prims and
// (tests) the rebuilt draw commands
struct StormContext {
    fx::TextureCache& tex;
    int rowBase;
    Frame& f;
    std::vector<fx::Prim>& prims;
    std::vector<DrawCommand>* commands;
};

// a 2D sprite of the PS1 frame (its texture registered in the cache); `original` false: a copy for wide windows
Sprite sprite(StormContext& c, int x, int y, int w, int h, int u, int v, uint16_t tpage, uint16_t clut, bool original) {
    Sprite s;
    s.x = int16_t(x);
    s.y = int16_t(y);
    s.w = uint16_t(w);
    s.h = uint16_t(h);
    s.u = uint8_t(u);
    s.v = uint8_t(v);
    s.tpage = tpage;
    s.clut = clut;
    s.texture = c.tex.index(tpage, clut);
    s.original = original;
    return s;
}

// ---- the script's backdrop, exe 0x517cf0 (script 6 state 0, and state 1 while shown) ------------------------------
// origin = the backdrop's origin of the scroll before this tick's (the scroll by the speeds comes last); sprite (i, j)
// at origin + (i, j) * 128, i outer, drawn when -0x81 < x < 0x141 and -0x81 < y < 0xf1 (the others are kept for wide
// windows); u = (j & 1) * 0x80, v 0x80
void backdrop(StormContext& c, std::vector<Sprite>& chain) {
    const port::ScriptState& script = port::scriptState();
    if (!(script.flags & port::kScriptRunning) || script.id != port::kCrashScript) return;
    const uint8_t state = script.state;
    const port::CrashScriptBytes bytes = port::crashBytes();
    if (state > uint8_t(port::CutsceneState::Steps) ||
        (state == uint8_t(port::CutsceneState::Steps) && bytes.backdropShown == 0))
        return;
    const uint16_t px = uint16_t(uint16_t(bytes.backdropX) - uint16_t(int16_t(bytes.backdropSpeedX)));
    const uint16_t py = uint16_t(uint16_t(bytes.backdropY) - uint16_t(int16_t(bytes.backdropSpeedY)));
    const int16_t x0 = port::backdropOriginX(px), y0 = port::backdropOriginY(py);
    for (int i = 0; i < kBackdropCells; ++i)
        for (int j = 0; j < kBackdropCells; ++j) {
            const int16_t x = int16_t(x0 + i * kBackdropCell), y = int16_t(y0 + j * kBackdropCell);
            const bool shown = x > -0x81 && x < 0x141 && y > -0x81 && y < 0xf1;
            if (!shown && (x <= -0x80 - kExtra || x >= 320 + kExtra || y <= -0x80 || y >= 240)) continue;
            chain.push_back(sprite(c, x, y, kBackdropCell, kBackdropCell, (j & 1) * 0x80, 0x80, kBackdropTpage,
                                   kBackdropClut, shown));
        }
}

// ---- the panorama of the cloud rings' variants exe 0x599a50 / exe 0x59a3e0 -----------------------------------------
// tiles: `rows` rows of 20 16x16 sprites from y `top` up by 16, u = the row's, v 0xe0; band: 11 sprites w 32 x `bandH`
// at y `bandY`: x = ((s >> 4) & 0x1f) - 0x20 + 32 i, u = (((s >> 4) >> 5) * 7 + i) * 32 (s = the ring's angle 1,
// already advanced by the update this tick)
void panorama(StormContext& c, const port::CloudRingFields& ring, std::vector<Sprite>& chain, const Entry& e, uint16_t clut,
              int rows, int top, const PanoramaRows& rowU, int bandY, int bandH) {
    for (int r = 0; r < rows; ++r)
        for (int k = -kExtra / 16; k < 20 + kExtra / 16; ++k)
            chain.push_back(sprite(c, k * 16, top - r * 16, 16, 16, rowU[size_t(r)], 0xe0, e.tpage, clut, k >= 0 && k < 20));
    const uint16_t s = uint16_t(int(ring.angles[1]) >> 4);
    const int x0 = int(s & 0x1f) - 0x20;
    const uint32_t col = (s >> 5) & 0xff;
    for (int i = -kExtra / 32; i < 11 + kExtra / 32; ++i)
        chain.push_back(sprite(c, int16_t(x0 + i * 32), bandY, 32, bandH, uint8_t((col * 7 + uint32_t(i)) * 32), 0,
                               e.tpage, clut, i >= 0 && i < 11));
}
// exe 0x599a50's sea, linked into the depth OT at slot exe 0x5f2f20(the static's position, 0, &slot) (its slot local
// starts at 0: off range, slot 0): 32 x 64 sprites at y 0xb0 for s = 0, 32 .. 0x140: x = s - (q & 0x1f), u = s +
// (q & 0xe0), v 0xa0 (q = the ring's angle 2 >> 2)
void sea(StormContext& c, const port::StaticActor& actor, const port::CloudRingFields& ring, const Entry& e,
         uint16_t clut) {
    DepthChain chain;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    port::projectedWidth(pos, 0, &chain.slot);
    for (int k = 0; k < 3; ++k) chain.centre[k] = float(pos[k]);
    const uint16_t q = uint16_t(int(ring.angles[2]) >> 2);
    for (int s = -kExtra; s < 0x160 + kExtra; s += 32)
        chain.sprites.push_back(sprite(c, int16_t(s - (q & 0x1f)), 0xb0, 32, 0x40, uint8_t(s + (q & 0xe0)), 0xa0,
                                       e.tpage, clut, s >= 0 && s < 0x160));
    c.f.depth.push_back(std::move(chain));
}

// ---- the cloud rings' variant exe 0x599580: four rings of 32 cloud puffs around the static -----------------------
// ring k: angle k of the ring (advanced this tick), its radius, y + its height offset, its size, CLUT x + its offset;
// puff j at angle a = ring + j * 0x80: (x + cos a * r >> 12, y, z + sin a * r >> 12), u0 = puffU[j & 7], v0 = the page
// y's low byte, 64 x 64 texels, rotation 0. Rings 1..3 skip the puffs within 0x2a1 of the camera yaw (+ 0x400 vs
// 0x1000 - a); exe 0x599050 culls on the game camera (depth 0x30 .. the effects' far * 4, screen -0x160 .. 0x2a0 /
// -0x88 .. 0x378).
void puffRings(StormContext& c, const port::StaticActor& actor, uint32_t object, const Entry& e) {
    const port::CloudRingFields& ring = port::classState<port::CloudRingFields>(actor);
    const int sh = port::stage().coordinateShift & 0x1f;
    const int camYaw = port::cameraState().viewAngles.y;
    for (uint32_t k = 0; k < kPuffRings; ++k) {
        const int ang = ring.angles[k];
        const int rad = puffRingRadii[k], size = puffSizes[k];
        const int16_t y = int16_t(int(puffRingHeights[k]) + int(actor.pos.y.whole));
        const uint16_t clut = clutWord(uint16_t(e.clutX + uint16_t(int16_t(puffRingClutX[k]))), e.clutY);
        const int tex = c.tex.index(e.tpage, clut);
        for (uint32_t j = 0; j < kPuffsPerRing; ++j) {
            const uint32_t an = uint32_t(ang + int(j) * 0x80) & 0xfff;
            if (k != 0 && std::abs(port::angleDiff(uint32_t(camYaw + 0x400), 0x1000u - an)) <= 0x2a1) continue;
            const int16_t x = int16_t((port::fixedCos(int(an)) * rad >> 12) + int(actor.pos.x.whole));
            const int16_t z = int16_t((port::fixedSin(int(an)) * rad >> 12) + int(actor.pos.z.whole));
            const int16_t p[3] = {int16_t(uint16_t(x) << sh), int16_t(uint16_t(y) << sh), int16_t(uint16_t(z) << sh)};
            const port::CameraPoint cp = port::cameraPoint(p);
            if (cp.view[2] > int32_t(uint32_t(port::stage().effectsFar) * 4) || cp.view[2] <= 0x2f) continue;
            if (uint16_t(cp.sx + 0x160) >= 0x401 || uint16_t(cp.sy + 0x88) >= 0x401) continue;
            // the billboard quad of exe 0x5f0580 with rotation 0: index (0 + 0x600) >> 6 = 24 of the 64-step tables
            // (cos / sin of 135 degrees)
            fx::Prim pr;
            pr.centre[0] = float(x);
            pr.centre[1] = float(y);
            pr.centre[2] = float(z);
            pr.object = object | k << 6 | j << 1; // one object a puff: the tick blending pairs the same puff
            pr.texture = tex;
            pr.blend = 0;
            const float ca = float(port::fixedCos64(24)) * float(size) / 4096.0f;
            const float sb = float(port::fixedSin64(24)) * float(size) / 4096.0f;
            const float off[4][2] = {{-ca, -sb}, {-sb, ca}, {sb, -ca}, {ca, sb}};
            const uint8_t u0 = puffU[j & 7], v0 = e.v0;
            const float uv[4][2] = {{float(u0), float(v0)}, {float(u0 + 0x3f), float(v0)}, {float(u0), float(v0 + 0x3f)},
                                    {float(u0 + 0x3f), float(v0 + 0x3f)}};
            for (int q = 0; q < 4; ++q) {
                pr.v[q].off[0] = off[q][0];
                pr.v[q].off[1] = off[q][1];
                pr.v[q].u = uv[q][0] + 0.5f;
                pr.v[q].v = uv[q][1] + 0.5f;
                pr.v[q].shade = 1.0f;
            }
            c.prims.push_back(pr);
        }
    }
}

// The gradient rows of the rings / glows (gradients(): colour at u 0 -> black at u 1)
constexpr uint32_t kRows[] = {0x0080ff, 0x004080, 0x008080, 0xc04040, 0x00ffff, 0xffffff, 0x3f7fff};

// colour c as reference r times a scale (each channel within 1): the scale, or < 0
float scaleOf(uint32_t c, uint32_t r) {
    uint32_t cm = 0, rm = 0;
    for (int k = 0; k < 3; ++k) {
        cm = std::max(cm, (c >> (8 * k)) & 0xff);
        rm = std::max(rm, (r >> (8 * k)) & 0xff);
    }
    if (rm == 0) return cm == 0 ? 0.0f : -1.0f;
    const float s = float(cm) / float(rm);
    for (int k = 0; k < 3; ++k)
        if (std::abs(float((r >> (8 * k)) & 0xff) * s - float((c >> (8 * k)) & 0xff)) > 1.01f) return -1.0f;
    return s;
}

// ---- the opening effect's flash ring (FlashRing), exe 0x436f00 ---------------------------------------------------
// the position = the screen point (projected once in the first state), the sub-state = the ticks drawn (advanced after
// the draw): with t = ticks - 1, colour c = (8 - t) * 0xff >> 3 at the centre (R c, G c >> 1, B c >> 2), black on the
// ellipse of radii rx = t * 32 + 0xb0, ry = rx * 3 / 4; 64 G3 from (x, y + ry) round the 64-step tables, additive
void flashRing(StormContext& c, const port::EffectActor& e) {
    if (port::OpeningEffectState(e.state) == port::OpeningEffectState::Start || e.subState == port::kFlashRingNoTicks ||
        e.subState > port::kFlashRingTicks)
        return;
    const uint32_t t = e.subState - 1u;
    const uint32_t col = ((8 - t) * 0xff) >> 3;
    const uint32_t centre = col | (col >> 1) << 8 | (col >> 2) << 16;
    const int rx = int(t * 32 + 0xb0), ry = int((uint32_t(rx) * 3) >> 2);
    const int16_t sx = e.pos.x, sy = e.pos.y;
    float shade = scaleOf(centre, 0x3f7fff); // the row (0xff, 0x7f, 0x3f) -> black at this brightness
    const int row = shade < 0 ? -1 : c.rowBase + int(std::size(kRows)) - 1;
    if (shade < 0) shade = 1.0f;
    int16_t px = sx, py = int16_t(sy + ry);
    for (uint32_t i = 1; i <= 0x40; ++i) {
        const int16_t nx = int16_t(sx + int16_t(uint32_t(port::fixedSin64(i) * rx) >> 12));
        const int16_t ny = int16_t(sy + int16_t(uint32_t(port::fixedCos64(i) * ry) >> 12));
        Tri tr;
        tr.row = row;
        tr.shade = shade;
        tr.x[0] = px, tr.y[0] = py, tr.colour[0] = 0;
        tr.x[1] = sx, tr.y[1] = sy, tr.colour[1] = centre;
        tr.x[2] = nx, tr.y[2] = ny, tr.colour[2] = 0;
        c.f.front.push_back(tr);
        px = nx;
        py = ny;
    }
}

// ---- draw commands (exe 0x5f0580) ---------------------------------------------------------------------------------
// The billboard textures the decoders below look up.
struct Tinted {
    uint16_t tpage, clut;
    uint32_t tint;
};
constexpr uint16_t kTrailTpage = 0x2f, kTrailClut = 0x7d50, kShotTpage = 0x2f, kShotClut = 0x7cd0;
constexpr Tinted kBillboards[] = {
    {kTrailTpage, kTrailClut, 0x004080}, // the debris fire trail (colour (c, c >> 1, 0))
    {kShotTpage, kShotClut, 0x808080},   // the enemy shot
};

// a billboard record as the storm draws it. Not game/draw_command_decode's decoder: the storm's records draw with the
// texture keyed by their colour's tint (kBillboards) and ring rows of their own (kRows), which the generic decoder does
// not look up. A billboard record: centre (s16 w2, w2 >> 16, w4), size w6 & 0xffff, rotation w6 >> 16, corners C - (a, b), C + (-b,
// a), C + (b, -a), C + (a, b) with a / b = the 64-step cos / sin [(rot + 0x600) >> 6] * size; uv w3 / w5 / w7 / w9,
// CLUT w3 >> 16, tpage w5 >> 16, colour w1 (bit 25 semi: the tpage's mode)
void decodeBillboard(StormContext& c, const uint32_t* w, uint32_t object) {
    const uint16_t clut = uint16_t(w[3] >> 16), tpage = uint16_t(w[5] >> 16);
    const uint32_t col = w[1] & 0xffffff;
    int tex = -1;
    float shade = 1.0f;
    for (const Tinted& t : kBillboards) {
        if (t.tpage != tpage || t.clut != clut) continue;
        const float s = scaleOf(col, t.tint);
        if (s < 0) continue;
        tex = c.tex.index(tpage, clut, t.tint);
        shade = s;
        break;
    }
    if (tex < 0) return;
    fx::Prim p;
    p.centre[0] = float(int16_t(w[2]));
    p.centre[1] = float(int16_t(w[2] >> 16));
    p.centre[2] = float(int16_t(w[4]));
    p.object = object;
    p.texture = tex;
    p.blend = ((w[1] >> 25) & 1) ? fx::blendOfAbr(tpage >> 5) : 0;
    const uint32_t i = ((uint32_t(int32_t(w[6]) >> 16) + 0x600) >> 6) & 0x3f;
    const float size = float(w[6] & 0xffff);
    const float a = float(port::fixedCos64(i)) * size / 4096.0f, b = float(port::fixedSin64(i)) * size / 4096.0f;
    const float off[4][2] = {{-a, -b}, {-b, a}, {b, -a}, {a, b}};
    const uint32_t uv[4] = {w[3], w[5], w[7], w[9]};
    for (int k = 0; k < 4; ++k) {
        p.v[k].off[0] = off[k][0];
        p.v[k].off[1] = off[k][1];
        p.v[k].u = float(uv[k] & 0xff) + 0.5f;
        p.v[k].v = float((uv[k] >> 8) & 0xff) + 0.5f;
        p.v[k].shade = shade;
    }
    c.prims.push_back(p);
}

// a ring record as the storm draws it (separate from game/draw_command_decode for the same reason: the colour picks one
// of the storm's gradient rows with a shade). A ring record: segments j = start + 1 .. end - 1 (start = (b0 - 0x10) >> s, end = ((b1 - 0x10) >> s) + 1; b0 / b1 /
// b2 / b3 = the bytes of w0, s = (w4 >> 16) & 0xff) of the 64-step circle at angle (j << s) + b2, x scaled by (0x80 -
// b3) / 128 and y by (0x80 + b3) / 128 (s8 b3); radius w5 & 0xffff with colour w1, radius w5 >> 16 with colour w3; one
// G4 per segment (in(j - 1), in(j), out(j - 1), out(j)) in the view plane; blend from w3 >> 24 (0x22: additive)
void decodeRing(StormContext& c, const uint32_t* w, uint32_t object) {
    const uint32_t cIn = w[1] & 0xffffff, cOut = w[3] & 0xffffff;
    if (cOut != 0) return; // every storm ring fades to black (other pairs have no row)
    int row = -1;
    float shade = 1.0f;
    for (size_t r = 0; r < std::size(kRows) && row < 0; ++r)
        if (const float sc = scaleOf(cIn, kRows[r]); sc >= 0) {
            row = c.rowBase + int(r);
            shade = sc;
        }
    if (row < 0) return;
    const int b0 = int(w[0] & 0xff), b1 = int((w[0] >> 8) & 0xff), b2 = int((w[0] >> 16) & 0xff);
    const int b3 = int(int8_t(w[0] >> 24));
    const int sft = int((w[4] >> 16) & 0x1f);
    const int start = (b0 - 0x10) >> sft, end = ((b1 - 0x10) >> sft) + 1;
    const float rIn = float(w[5] & 0xffff), rOut = float(int32_t(w[5]) >> 16);
    const float sx = float(0x80 - b3) / 128.0f, sy = float(0x80 + b3) / 128.0f;
    auto at = [&](int j, float r, float t) {
        const uint32_t a = uint32_t((j << sft) + b2) & 0x3f;
        fx::Vertex v;
        v.off[0] = float(port::fixedCos64(a)) * sx * r / 4096.0f;
        v.off[1] = float(port::fixedSin64(a)) * sy * r / 4096.0f;
        v.u = 0.5f + t * 255.0f;
        v.v = float(row) + 0.5f;
        v.shade = shade;
        return v;
    };
    for (int j = start + 1; j < end; ++j) {
        fx::Prim p;
        p.centre[0] = float(int16_t(w[2]));
        p.centre[1] = float(int16_t(w[2] >> 16));
        p.centre[2] = float(int16_t(w[4]));
        p.object = object;
        p.texture = -1;
        p.blend = fx::blendOfAbr(((w[3] >> 24) & 0xf0) >> 5);
        p.v[0] = at(j - 1, rIn, 0.0f);
        p.v[1] = at(j, rIn, 0.0f);
        p.v[2] = at(j - 1, rOut, 1.0f);
        p.v[3] = at(j, rOut, 1.0f);
        c.prims.push_back(p);
    }
}

// one record as the exe's emitter at `pc` writes it, decoded as the draw pass does
template <size_t N> void command(StormContext& c, uint32_t pc, const std::array<uint32_t, N>& w, uint32_t object) {
    if (c.commands) c.commands->push_back({pc, std::vector<uint32_t>(w.begin(), w.end())});
    const uint32_t opcode = (w[1] >> 24) & dc::kRecordOpcodeMask;
    if (opcode == dc::kBillboardRecord) decodeBillboard(c, w.data(), object);
    else if (opcode == dc::kRingRecord) decodeRing(c, w.data(), object);
}
// a disc (the full ring from the colour to black, additive, segments 4 steps apart) at (x, y, z) of radius r
dc::RingWords glowDisc(uint32_t colour, uint32_t xy, uint32_t zWord, uint32_t r) {
    dc::Ring ring;
    ring.inner = dc::ringColour(colour);
    ring.xy = xy;
    ring.zStep = zWord | dc::ringStep(2);
    ring.radii = dc::ringRadii(0, r);
    return dc::recordWords(ring);
}

// exe 0x5cc150(actor, dz): 16 billboards along z from z + dz back by (0x100 - 16 i) >> 5, colour c = (0x800 - i * 0x80)
// >> 4 as (c, c >> 1, 0), semi, size (0x100 - 16 i) >> 4, the trail's texture, uv (0x20, 0x20)-(0x3f, 0x3f)
void debrisTrail(StormContext& c, int16_t x, int16_t y, int16_t z, int16_t dz, uint32_t object) {
    int16_t zz = int16_t(z + dz);
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t col = (0x800 - i * 0x80) >> 4;
        const uint32_t size = (0x100 - i * 16) >> 4;
        dc::Billboard b;
        b.colour = dc::billboardColour((col & 0x1fe) << 7 | col);
        b.xy = dc::packXY(x, y);
        b.z = dc::signedZWord(zz);
        b.clut = kTrailClut;
        b.tpage = kTrailTpage;
        b.uv[0] = dc::texel(0x20, 0x20);
        b.uv[1] = dc::texel(0x3f, 0x20);
        b.uv[2] = dc::texel(0x20, 0x3f);
        b.uv[3] = dc::texel(0x3f, 0x3f);
        b.sizeRotation = dc::sizeRotation(size, 0);
        command(c, 0x5cc1d5, dc::recordWords(b), object);
        zz = int16_t(zz - int16_t((0x100 - i * 16) >> 5));
    }
}

// the falling piece (exe 0x5cbf00): drawn at the position before this tick's move (the copy the update makes first)
// with its tick counter before its increment
void debris(StormContext& c, const port::ProjectileActor& piece, uint32_t object) {
    const RoutineId routine = port::pieceRoutines->idAt(piece.variant);
    if (piece.state == uint8_t(port::FallingPieceState::Spawn)) return;
    const port::FallingPieceFields& f = port::classState<port::FallingPieceFields>(piece);
    const int16_t x = piece.prevPos.x.whole, y = piece.prevPos.y.whole, z = piece.prevPos.z.whole;
    const int32_t n = int32_t(f.ticks) - 1;
    if (routine == RoutineId::CrashPieceDives) {
        if (n == 2 || n == 3) // the glow disc: 0x0080ff radius 0x80 at z + 0x100, then 0x004080 radius 0x40 at z + 0x40
            command(c, 0x5cbffa,
                    glowDisc(n == 2 ? 0x0080ffu : 0x004080u, dc::packXY(x, y), dc::zWord(int16_t(z + (n == 2 ? 0x100 : 0x40))),
                             n == 2 ? 0x80u : 0x40u),
                    object);
        if (n > 2) debrisTrail(c, x, y, z, 0x30, object | 1);
    } else if (routine == RoutineId::CrashPieceFalls) {
        debrisTrail(c, x, y, z, 0x10, object | 1);
    }
}

// the flash's rings, exe 0x431750 (the flash exe 0x431120 in its glow state; its step counted down after the draw): t
// = step + 1, colour ((shadeB * t * 32) & ~0xff | (shadeA * t * 32) >> 8), r = size * (12 - t) >> 4: rings (r / 2 ->
// 3r / 8), (r / 2 -> 5r / 8) and a disc to r / 2, all to black, additive
void flash(StormContext& c, const port::EffectActor& e, uint32_t object) {
    // the spawn tick (state Start) leaves the step at kFlashTicks without drawing; the draws see 6..1 and leave 5..0
    if (e.state != uint8_t(port::CrashFlashState::Glow) || e.step >= port::kFlashTicks) return;
    const port::CrashFlashFields& f = port::classState<port::CrashFlashFields>(e);
    const uint32_t t = uint32_t(e.step) + 1u, k = t * 0x20;
    const uint32_t col = ((uint32_t(f.shadeB) * k) & 0xffffff00u) | (uint32_t(f.shadeA) * k) >> 8;
    const uint32_t r = uint32_t(int32_t(uint32_t(f.size) * (0xc - t)) >> 4), h = r >> 1;
    dc::Ring ring;
    ring.inner = dc::ringColour(col);
    ring.xy = e.positionWord(0);
    ring.zStep = dc::zWord(e.pos.z) | dc::ringStep(2);
    ring.radii = h | ((h * 3) & 0xfffc) << 14; // h -> (3h >> 2) & 0x3fff
    command(c, 0x4317a4, dc::recordWords(ring), object);
    ring.radii = h | ((h * 5) & 0xfffc) << 14; // h -> (5h >> 2) & 0x3fff
    command(c, 0x4317a7, dc::recordWords(ring), object);
    ring.radii = (r & 0xfffe) << 15; // 0 -> r >> 1
    command(c, 0x4317aa, dc::recordWords(ring), object);
}

// the enemy shot (exe 0x5c52f0): the spawn tick (state 0 -> 1; the flying state sets airborne at every drawn tick)
// draws exe 0x5c6fc0's glow disc (0x008080 to black, radius (variant & 3) * 0x20 + 0x30); the flying state exe
// 0x5c5440 one billboard after its move (colour 0x808080, opaque, the shot's texture, uv (0xc0, 0x60)-(0xdf, 0x7f),
// size (variant & 3) * 16 + 0x18, rotation ticks << 8)
void enemyShot(StormContext& c, port::ProjectileActor& shot, uint32_t object) {
    if (port::EnemyShotState(shot.state) != port::EnemyShotState::Fly) return;
    const port::EnemyShotFields f(shot);
    const uint32_t v = shot.variant & port::kShotSizeMask;
    const uint32_t xy = dc::packXY(shot.pos.x.whole, shot.pos.y.whole);
    if (shot.airborne == 0) {
        command(c, 0x5c6fcc, glowDisc(0x008080, xy, dc::zWord(shot.pos.z.whole), v * 0x20 + 0x30), object);
        return;
    }
    dc::Billboard b;
    b.colour = dc::opaqueBillboardColour(0x808080);
    b.xy = xy;
    b.z = dc::signedZWord(shot.pos.z.whole);
    b.clut = kShotClut;
    b.tpage = kShotTpage;
    b.uv[0] = dc::texel(0xc0, 0x60);
    b.uv[1] = dc::texel(0xdf, 0x60);
    b.uv[2] = dc::texel(0xc0, 0x7f);
    b.uv[3] = dc::texel(0xdf, 0x7f);
    b.sizeRotation = dc::sizeRotation((v << 4) + 0x18, (uint32_t(uint16_t(f.ticks)) << 8) & 0xfff);
    command(c, 0x5c54e0, dc::recordWords(b), object);
}

// the landing craft's jets, exe 0x4bbec0 (the crash craft exe 0x4bb750's landing variant, state 2 from the script step
// tick kCraftJetsTick; its exhaust clock counted after): while (clock & 3) <= 1, a disc 0xc04040 -> black of radius 0x80
// - ((clock & 3) << 6) at each jet R(rotation) * (+-kExhaustSide, 0, kExhaustBack) from the position before the move
// (the previous position, the previous rotation)
void craftJets(StormContext& c, const port::AnimatedActor& craft, uint32_t object) {
    if (port::craftVariants->idAt(craft.variant) != RoutineId::CrashCraftLanding || port::CraftState(craft.state) != port::CraftState::Act2) return;
    const port::ScriptState& script = port::scriptState();
    if (script.stepTick < kCraftJetsTick) return;
    const port::CraftFields& f = port::classState<port::CraftFields>(craft);
    const uint32_t n = uint32_t(uint16_t(f.exhaustClock - 1u));
    if ((n & 3) > 1) return;
    const int16_t rot[3] = {craft.prevRotation.x, craft.prevRotation.y, craft.prevRotation.z};
    for (int i = 0; i < 2; ++i) {
        int32_t v[3];
        port::rotateVector(rot, i == 0 ? port::kExhaustSide : -port::kExhaustSide, 0, port::kExhaustBack, v);
        const uint32_t px = uint32_t(v[0]) + uint32_t(craft.prevPos.x), py = uint32_t(v[1]) + uint32_t(craft.prevPos.y),
                       pz = uint32_t(v[2]) + uint32_t(craft.prevPos.z);
        command(c, 0x4bbfe3, glowDisc(0xc04040, (px >> 16) | (py & 0xffff0000u), pz >> 16, 0x80 - ((n & 3) << 6)),
                object | uint32_t(i));
    }
}

} // namespace

std::vector<std::array<uint32_t, 2>> gradients() {
    std::vector<std::array<uint32_t, 2>> g;
    for (uint32_t c : kRows) g.push_back({c, 0});
    return g;
}

std::vector<fx::Texture> textures(int stage) {
    std::vector<fx::Texture> t;
    if (stage != 0x3a || !cloudEntries.available()) return t;
    auto add = [&](uint16_t tpage, uint16_t clut) {
        for (const fx::Texture& x : t)
            if (x.tpage == tpage && x.clut == clut) return;
        t.push_back({tpage, clut, fx::kNeutralTint});
    };
    add(kBackdropTpage, kBackdropClut);
    for (const Tinted& b : kBillboards) t.push_back({b.tpage, b.clut, b.tint});
    for (uint32_t e : {3u, 7u}) // the entries of ST3A's statics (the puff rings' -> 7, the panoramas' -> 3)
        for (bool alt : {false, true}) {
            const Entry en = entry(e, alt);
            add(en.tpage, clutWord(en.clutX, en.clutY));
            if (e == 7)
                for (uint32_t k = 0; k < kPuffRings; ++k)
                    add(en.tpage, clutWord(uint16_t(en.clutX + uint16_t(int16_t(puffRingClutX[k]))), en.clutY));
        }
    return t;
}

void collect(fx::TextureCache& tex, int rowBase, Frame& frame, std::vector<fx::Prim>& prims,
             std::vector<DrawCommand>* commands) {
    frame.clear();
    // any stage: the backdrop / panorama / puffs need script 6 / the cloud rings (only met in ST3A), the draw-command
    // classes below draw wherever they live; their textures come from the stage's VRAM through the cache
    StormContext c{tex, rowBase, frame, prims, commands};
    std::vector<std::vector<Sprite>> slot88; // chains in insertion order (each one goes to the head of the slot)
    slot88.emplace_back();
    backdrop(c, slot88.back()); // the script runs before the pools (the frame body)
    port::StaticPool& statics = port::staticPool();
    for (int late = 0; late < 2; ++late)
        for (uint32_t i = 0; i < statics.size(); ++i) {
            const port::StaticActor& a = statics[i];
            if (!a.live() || a.state != uint8_t(port::CloudRingState::Turn)) continue;
            const RoutineId h =
                port::actorHandler(a, late ? port::ActorPool::StaticsLate : port::ActorPool::Statics).id();
            if (h != kCloudRingClass) continue;
            const port::CloudRingFields& ring = port::classState<port::CloudRingFields>(a);
            const Entry e = entry(ring.textureEntry, ring.altClut != 0);
            const uint16_t clut = clutWord(e.clutX, e.clutY);
            switch (port::cloudRoutines->idAt(a.variant)) {
            case RoutineId::CrashCloudSprites: puffRings(c, a, kCloudObjects | i << 8, e); break;
            case RoutineId::CrashCloudYawFollow:
                slot88.emplace_back();
                panorama(c, ring, slot88.back(), e, clut, 5, 0x40, *seaPanoramaRows, 0x50, 0xa0);
                sea(c, a, ring, e, clut);
                break;
            case RoutineId::CrashCloudYawFollowOne:
                slot88.emplace_back();
                panorama(c, ring, slot88.back(), e, clut, 10, 0x90, *skyPanoramaRows, 0xa0, 0x50);
                break;
            default: break; // the other variants: not met in ST3A
            }
        }
    for (auto it = slot88.rbegin(); it != slot88.rend(); ++it) frame.back.insert(frame.back.end(), it->begin(), it->end());
    port::EffectPool& effects = port::effectPool();
    for (uint32_t i = 0; i < effects.size(); ++i) { // pool 0xa0 (both loops): the flash ring, the flash
        const port::EffectActor& a = effects[i];
        if (!a.live()) continue;
        RoutineRef h = port::actorHandler(a, port::ActorPool::Effects);
        if (!h) h = port::actorHandler(a, port::ActorPool::EffectsLate);
        if (h.is(kOpeningEffectClass) && a.variant == uint8_t(port::OpeningEffect::FlashRing)) flashRing(c, a);
        else if (h.is(kCrashFlashClass)) flash(c, a, kFlashObjects | i << 4);
    }
    port::ProjectilePool& projectiles = port::projectilePool();
    for (uint32_t i = 0; i < projectiles.size(); ++i) { // pool 0x40: the falling pieces, the enemy shots
        port::ProjectileActor& a = projectiles[i];
        if (!a.live()) continue;
        const RoutineId h = port::actorHandler(a, port::ActorPool::Projectiles).id();
        if (h == kFallingPieceClass) debris(c, a, kProjectileObjects | i << 4);
        else if (h == kEnemyShotClass) enemyShot(c, a, kProjectileObjects | i << 4);
    }
    port::AnimatedPool& animated = port::animatedPool();
    for (uint32_t i = 0; i < port::kAnimatedFieldSlots; ++i) { // pool 0x20: the crash craft's exhaust glows
        const port::AnimatedActor& a = animated[i];
        if (!a.live()) continue;
        if (port::fieldActorHandler(a, port::ActorPool::Animated).is(kCrashCraftClass))
            craftJets(c, a, kCraftObjects | i << 4);
    }
}

} // namespace od::storm
