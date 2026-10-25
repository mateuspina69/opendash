#include "game/actor_state.hpp"
#include "game/effects_draw.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/content_registry.hpp"
#include "game/explosions.hpp"
#include "game/hit_effects.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/reticle_draw.hpp"
#include "game/shots/blade_arm.hpp"
#include "game/shots/buster.hpp"
#include "game/shots/extinguisher.hpp"
#include "game/shots/player_shots.hpp"
#include "game/shots/shining_laser.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::fx {
namespace {

using port::fixedCos;
using port::fixedSin;

// the game classes drawn here (their update routines)
constexpr RoutineId kBusterShotClass = RoutineId::BusterShotUpdate,
                    kExtinguisherClass = RoutineId::ExtinguisherShotUpdate;
constexpr RoutineId kHitSparkClass = RoutineId::HitSparkUpdate, kBurstClass = RoutineId::BurstUpdate,
                    kBladeTrailClass = RoutineId::BladeTrailUpdate, kLaserBeamClass = RoutineId::ShiningLaserBeamUpdate;
constexpr RoutineId kPickupClass = RoutineId::PickupUpdate, kExplosionClass = RoutineId::ExplosionUpdate,
                    kHurtSparksClass = RoutineId::HurtSparksUpdate;
// the prims' object ids: a base by class and the slot's index
constexpr uint32_t kShotObjects = 0x10000u, kStreamObjects = 0x18000u, kEffectObjects = 0x20000u,
                   kBladeTrailObjects = 0x24000u, kLaserObjects = 0x26000u, kStaticObjects = 0x30000u;

// the textures: the buster shot's (tpage 0x2f additive / 0x0f average, CLUT 0x7c92, uv (0, 0x20)-(0x1f, 0x3f)), the
// explosion sheet (tpage 0x2e, CLUT 0x7c10: 48 x 48 frames), the pickups' glow (tpage 0x2f, CLUT 0x7c12, tint 0x008080)
// and the extinguisher's stream (tpage 0x2f, CLUT 0x7c12)
constexpr uint16_t kShotTpage = 0x2f, kShotGlowTpage = 0x0f, kShotClut = 0x7c92;
constexpr uint16_t kExplosionTpage = 0x2e, kExplosionClut = 0x7c10;
constexpr uint16_t kGlowTpage = 0x2f, kPickupGlowClut = 0x7c12, kStreamClut = 0x7c12;
constexpr uint32_t kPickupGlowTint = 0x008080;
// the Shining Laser's beam: tpage 0x2e (additive), the tube's CLUT and the rings'
constexpr uint16_t kLaserTpage = 0x2e, kLaserTubeClut = 0x7d51, kLaserRingClut = 0x7d52;
// gradient rows (gradients())
constexpr int kRowGrey = 0, kRowBurst = 1, kRowShot = 5, kRowHurt = 9, kRowPickup = 10, kPickupPairs = 9;

// the tables the draws read (the data source's): the buster shot's colours (4 by size >> 1, exe 0x8c4908), the pickups'
// colour pairs {a, b} (by the colour index, exe 0x91b9f4) and models (exe 0x91b9e8: 3 pointers to 16 s16 x / y / z /
// pad vertices: crystal, small crystal, cube), the Blade Arm's 16 trail colours (exe 0x8c3f20)
const DataTable<ScalarTable<uint32_t, 4>> shotColours{TableId::BusterShotColours};
const DataTable<ScalarTable<uint32_t, 2 * kPickupPairs>> pickupColours{TableId::PickupColours};
struct PickupModel {
    static constexpr size_t kPoints = 16;
    bool present = false; // the pointer names a whole model
    std::array<std::array<int16_t, 3>, kPoints> points{};
};
struct PickupModels : DataRecord {
    static constexpr uint32_t kBytes = 3 * 4;
    std::array<PickupModel, 3> models{};
    static PickupModels load(const TableReader& r) {
        PickupModels t;
        for (size_t i = 0; i < t.models.size(); ++i) {
            const TableReader m = r.follow(i * 4, PickupModel::kPoints * 8);
            PickupModel& model = t.models[i];
            model.present = m.size() == PickupModel::kPoints * 8;
            if (!model.present) continue;
            for (size_t k = 0; k < PickupModel::kPoints; ++k)
                model.points[k] = {m.s16(k * 8), m.s16(k * 8 + 2), m.s16(k * 8 + 4)};
        }
        return t;
    }
};
const DataTable<PickupModels> pickupModels{TableId::PickupModels};
const DataTable<ScalarTable<uint32_t, 0x10>> bladeTrailColours{TableId::BladeTrailColours};
constexpr int kRowReticle = kRowPickup + kPickupPairs; // 4 rows: town, town zooming, field in range, out of range
constexpr int kRowStream = kRowReticle + 4;            // the extinguisher stream's glow disc 0xc0c0b0
constexpr int kGameRows = kRowStream + 1;              // the content's rows follow
constexpr uint32_t kReticleColours[4] = {0xe0e000u, 0xa0a000u, 0x00e0e0u, 0x0000e0u};
constexpr uint32_t kHurtInner = 0x00ffff, kHurtOuter = 0x0000ff, kStreamGlow = 0xb0c0c0;

int findTexture(const std::vector<Texture>& t, uint16_t tpage, uint16_t clut, uint32_t tint) {
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].tpage == tpage && t[i].clut == clut && t[i].tint == (tint & 0xffffff)) return int(i);
    return -1;
}

void centreS16(const port::ShortVec3& p, float out[3]) { // s16 x / y / z
    out[0] = float(p.x);
    out[1] = float(p.y);
    out[2] = float(p.z);
}
void centre1616(const port::Actor& a, float out[3]) { // the 16.16 position's integer parts
    out[0] = float(a.pos.x.whole);
    out[1] = float(a.pos.y.whole);
    out[2] = float(a.pos.z.whole);
}

// ---- pool 0x10: the buster shot's flight exe 0x56c310 (two billboards) + exe 0x56c610 (the glow ring) -------------
void shot(DrawContext& c, const port::ShotActor& a, uint32_t object) {
    if (port::BusterState(a.state) != port::BusterState::Fly) return;
    const port::BusterShotFields& f = port::classState<port::BusterShotFields>(a);
    const uint32_t size = uint16_t(f.stats.level), range = f.stats.range;
    float ctr[3];
    centre1616(a, ctr);
    const uint32_t k = std::min<uint32_t>(size >> 1, 3);
    const float s = float(size * 8 + 0x20);
    const uint32_t rot = (range & 0x1f) << 7;
    // sprite 1: the colour by the size, tpage 0x2f (additive); sprite 2: neutral, tpage 0x0f (average)
    billboard(c, object, ctr, s, rot, c.tex.index(kShotTpage, kShotClut, shotColours[k]), blendOfAbr(kShotTpage >> 5),
              0, 0x20, 0x1f, 0x3f);
    billboard(c, object, ctr, s, rot, c.tex.index(kShotGlowTpage, kShotClut, kNeutralTint), blendOfAbr(kShotGlowTpage >> 5), 0,
              0x20, 0x1f, 0x3f);
    ring(c, object, ctr, 0.0f, s, kRowShot + int(k), 1.0f);
}

// ---- pool 0xa0 type 0: the hit spark exe 0x55d280 (a billboard of its frame script) --------------------------------
void spark(DrawContext& c, const port::EffectActor& a, uint32_t object) {
    if (port::HitSparkState(a.state) != port::HitSparkState::Play) return;
    const port::HitSparkFields& f = port::classState<port::HitSparkFields>(a);
    const uint32_t v = a.variant;
    if (!f.frame.targetMapped(12)) return;
    const port::SparkFrame fr = *f.frame;
    const port::SparkStyle& style = port::sparkStyles()[v];
    const uint16_t tpage = style.tpage, clut = style.clut;
    const bool semi = (style.semi & 1) || (fr.code & 2);
    float ctr[3];
    centreS16(a.pos, ctr);
    const float size = float(uint32_t(f.size) + fr.sizeAdd);
    billboard(c, object, ctr, size, uint32_t(fr.rotation) << 4, c.tex.index(tpage, clut, fr.colour & 0xffffff),
              semi ? blendOfAbr(tpage >> 5) : 0, fr.u0, fr.v0, fr.u1, fr.v1);
}

// ---- pool 0xa0 type 1: the burst, exe 0x55d4e0 (variant 0: 2 glow discs + 16 spokes) / exe 0x55d950 (variant 1: rings)
void burst(DrawContext& c, const port::EffectActor& a, uint32_t object) {
    const port::BurstFields& f = port::classState<port::BurstFields>(a);
    const port::BurstState state = port::BurstState(a.state);
    const uint32_t post = f.ticks;
    if ((state != port::BurstState::Spokes && state != port::BurstState::Plain) || post >= port::kBurstTicks)
        return; // kBurstTicks = initialised this tick (drawn from the next one)
    const uint32_t ft = post + 1; // the value the draw read (before the count down)
    const int row = kRowBurst + int(std::min<uint32_t>(f.sizeClass >> 1, 3));
    float ctr[3];
    centreS16(a.pos, ctr);
    const uint32_t size = f.size;
    if (state == port::BurstState::Spokes) {
        const uint32_t g = std::min<uint32_t>(ft * 0x40 + 0xf, 0xff);
        const float r = float(int((0xc - ft) * size) >> 4);
        ring(c, object, ctr, 0, r * 0.5f, kRowGrey, float(g) / 255.0f);
        ring(c, object, ctr, 0, r, row, float(g) / 256.0f);
        const float spoke = float(g >> 1) / 256.0f;
        for (uint32_t k = 0; k < 16; ++k) {
            const uint32_t b = f.spokes[k], ang = b & 0x3f;
            const float len = float(int(((b >> 6) + 4) * (8 - ft) * size) >> 5), w = float(int(len) >> 4);
            Prim p;
            std::copy(ctr, ctr + 3, p.centre);
            p.object = object;
            p.blend = 2;
            const uint32_t l = (ang - 8) & 0x3f, r8 = (ang + 8) & 0x3f;
            p.v[0] = grad(tableCos(l) * w / 4096.0f, tableSin(l) * w / 4096.0f, row, 1, spoke);
            p.v[1] = grad(tableCos(ang) * len / 4096.0f, tableSin(ang) * len / 4096.0f, row, 1, spoke);
            p.v[2] = grad(0, 0, row, 0, spoke);
            p.v[3] = grad(tableCos(r8) * w / 4096.0f, tableSin(r8) * w / 4096.0f, row, 1, spoke);
            c.out.push_back(p);
        }
    } else {
        const float r = float(int(size * (0xc - ft)) >> 4);
        const float c1 = float(ft * 0x10) / 256.0f, c2 = std::min(float(ft * 0x20) / 256.0f, 1.0f);
        ring(c, object, ctr, r, r - 16, row, c1);
        ring(c, object, ctr, r, r + 16, row, c1);
        ring(c, object, ctr, 0, r - 16, row, c2);
    }
}

// ---- pool 0x60 type 8: the death explosion, exe 0x5645b0 (4 flash billboards, 48 x 48 frames of the sheet) ---------
void explosion(DrawContext& c, State& st, const port::StaticActor& a, uint32_t object) {
    const port::ExplosionFields& f = port::classState<port::ExplosionFields>(a);
    std::array<int8_t, 4> now{};
    for (uint32_t k = 0; k < 4; ++k) now[k] = f.lives[k];
    const auto it = st.flashes.find(a.at);
    const std::array<int8_t, 4> prev = it != st.flashes.end() ? it->second : std::array<int8_t, 4>{};
    st.flashes[a.at] = now;
    if (port::ExplosionState(a.state) != port::ExplosionState::Run) return;
    const int tex = c.tex.index(kExplosionTpage, kExplosionClut, kNeutralTint);
    for (uint32_t k = 0; k < 4; ++k) {
        // the draw reads the counter, then ages it: drawn value = post + 1 (1 when it just reached 0)
        const int cd = now[k] > 0 ? now[k] + 1 : (prev[k] == 1 ? 1 : 0);
        if (cd <= 0 || cd > 8) continue;
        const uint32_t fr = uint32_t(8 - cd);
        const port::ExplosionFlash flash = f.flashes[k];
        const int u = int(fr & 3) * 0x30, v = int(fr & 4) * 0xc;
        float ctr[3];
        centreS16(flash.point, ctr);
        billboard(c, object, ctr, float(uint16_t(flash.size)), 0, tex, blendOfAbr(kExplosionTpage >> 5), u, v, u + 0x2f,
                  v + 0x2f);
    }
}

// rotation Ry * Rx (angles in 4096 units) applied to (0, 0, d): the game's exe 0x5e3970 (game/matrix rotateVector)
void rotZ(float ax, float ay, float d, float out[3]) {
    const float x = ax * 6.2831853f / 4096.0f, y = ay * 6.2831853f / 4096.0f;
    out[0] = std::sin(y) * std::cos(x) * d;
    out[1] = -std::sin(x) * d;
    out[2] = std::cos(y) * std::cos(x) * d;
}

// ---- the Blade Arm's trail: pool 0xa0 global type 5 (exe 0x55e870), exe 0x55e8f0 -------------------------------------
// Reads the swing's block (game/shots/blade_arm): the range, the level, the ring's newest entry and per entry the point,
// the pitch and the yaw. Untextured gouraud, additive (tpage 0x220), colours cycling through the 16 of the table with the
// frame counter 0x6d0006 (here the draw tick).
//  * the beam: from the newest point P along d = Ry Rx(pitch, yaw) (0, 0, -range) / 16, segments P -> P + d -> P + 2d;
//    each is 4 G4 fins from the centre line (colour [(seg - frame) & 15] -> [(seg + 1 - frame) & 15]) out to the
//    screen-space corners (+-w, +-w), black; w = the projected width of level * 4 + 4 units, 0 at the far end;
//  * the ribbon: for the 7 newest ring pairs (e, e - 1), e = (index + k) & 7, k = 8 .. 2 (brightness k * 16): 4
//    steps j interpolating the points and the angles (12-bit wrapped), each a strip of 3 G4 quads out along the blade
//    (steps 1/2, 1, 1/2 of the 2 * range blade), colour lerp([(-8 - frame - k) & 15], [(-7 - frame - k) & 15], t / 4)
//    times min(255, s * (k * 16 - j * 4 [- 4 on the far edge]) * 7 >> 4) / 1024 at the s-th point; the tip black.
void bladeTrail(DrawContext& c, const State& st, const port::EffectActor& e, uint32_t object) {
    const port::Player& player = port::player();
    if (port::BladeTrailState(e.state) != port::BladeTrailState::Follow ||
        !(player.armFlags & port::kArmSpecial) || player.specialFiring == 0)
        return;
    const port::BladeTrailFields& trail = port::classState<port::BladeTrailFields>(e);
    if (!trail.swingShot) return;
    const port::BladeFields& blade = port::classState<port::BladeFields>(*trail.swingShot);
    const uint32_t idx = blade.ringIndex & 7;
    const float range = float(blade.stats.range) / 16.0f; // exe 0x5e3970 is (v << 12) in 16.16: the integer part v / 16
    auto colour = [&](uint32_t k) { return uint32_t(bladeTrailColours[k & 0xf]); };
    auto point = [&](uint32_t i, float out[3]) { centreS16(blade.ringPoints[i & 7], out); };
    auto fill = [](Vertex& v, uint32_t col) { // PS1 colour 0xBBGGRR -> untextured multiplier (/ 255)
        for (int q = 0; q < 3; ++q) v.color[q] = float((col >> (q * 8)) & 0xff) / 255.0f;
    };
    const uint32_t frame = st.tick;

    float p[3], d[3];
    point(idx, p);
    rotZ(float(blade.ringPitches[idx]), float(blade.ringYaws[idx]), -range, d);
    const float w = float(uint16_t(blade.stats.level) * 4 + 4);
    static constexpr float kFin[4][2] = {{-1, -1}, {1, 1}, {1, -1}, {-1, 1}};
    for (uint32_t seg = 0; seg < 2; ++seg) {
        const float ws = w, we = seg == 1 ? 0.0f : w;
        const uint32_t cs = colour(seg - frame), ce = colour(seg + 1 - frame);
        for (const auto& f : kFin) {
            Prim pr;
            for (int q = 0; q < 3; ++q) pr.centre[q] = p[q] + d[q] * float(seg);
            pr.texture = kUntextured;
            pr.blend = 2; // PS1 additive
            pr.object = object;
            Vertex* v = pr.v;
            v[0].off[0] = f[0] * ws;
            v[0].off[1] = f[1] * ws;
            fill(v[0], 0);
            fill(v[1], cs);
            for (int q = 0; q < 3; ++q) v[2].woff[q] = v[3].woff[q] = d[q];
            v[2].off[0] = f[0] * we;
            v[2].off[1] = f[1] * we;
            fill(v[2], 0);
            fill(v[3], ce);
            c.out.push_back(pr);
        }
    }

    auto wrap12 = [](int v) { return int(int16_t(uint16_t(v << 4))) >> 4; };
    for (uint32_t k = 8; k > 1; --k) {
        const uint32_t ei = (idx + k) & 7, pi = (ei - 1) & 7;
        float a0[3], a1[3];
        point(ei, a0);
        point(pi, a1);
        const int pitch = blade.ringPitches[ei], yaw = blade.ringYaws[ei];
        const int dp = wrap12(int(uint16_t(blade.ringPitches[pi])) - int(uint16_t(blade.ringPitches[ei])));
        const int dy = wrap12(int(uint16_t(blade.ringYaws[pi])) - int(uint16_t(blade.ringYaws[ei])));
        const uint32_t c0 = colour(uint32_t(-8) - frame - k), c1 = colour(uint32_t(-7) - frame - k);
        // the colour at step t (j or j + 1), point s along the blade, brightness lv
        auto shade = [&](int t, int s, int lv) {
            const uint32_t i = std::min<uint32_t>(uint32_t(s * lv * 7) >> 4, 0xff);
            uint32_t out = 0;
            for (int q = 0; q < 3; ++q) {
                const uint32_t x0 = (c0 >> (q * 8)) & 0xff, x1 = (c1 >> (q * 8)) & 0xff;
                const uint32_t sum = x1 * uint32_t(t) + x0 * uint32_t(4 - t);
                out |= (((sum * i) >> 10) & 0xff) << (q * 8);
            }
            return out;
        };
        for (int j = 0; j < 4; ++j) {
            float dir[2][3], at[2][3];
            for (int h = 0; h < 2; ++h) {
                const int t = j + h;
                rotZ(float(int16_t(pitch + ((dp * t) >> 2))), float(int16_t(yaw + ((dy * t) >> 2))), -range, dir[h]);
                for (int q = 0; q < 3; ++q) at[h][q] = a0[q] + float(int((a1[q] - a0[q]) * float(t)) >> 2);
            }
            const int bright = int(k) * 16 - j * 4;
            for (int s = 0; s < 3; ++s) {
                const float step = float((s & 1) + 1) * 0.5f;
                float next[2][3];
                for (int h = 0; h < 2; ++h)
                    for (int q = 0; q < 3; ++q) next[h][q] = at[h][q] + dir[h][q] * step;
                Prim pr;
                std::copy(at[0], at[0] + 3, pr.centre);
                pr.screen = false;
                pr.texture = kUntextured;
                pr.blend = 2;
                pr.object = object;
                for (int q = 0; q < 3; ++q) {
                    pr.v[1].off[q] = at[1][q] - at[0][q];
                    pr.v[2].off[q] = next[0][q] - at[0][q];
                    pr.v[3].off[q] = next[1][q] - at[0][q];
                }
                fill(pr.v[0], shade(j, s, bright));
                fill(pr.v[1], shade(j + 1, s, bright - 4));
                fill(pr.v[2], s == 2 ? 0 : shade(j, s + 1, bright));
                fill(pr.v[3], s == 2 ? 0 : shade(j + 1, s + 1, bright - 4));
                c.out.push_back(pr);
                for (int h = 0; h < 2; ++h) std::copy(next[h], next[h] + 3, at[h]);
            }
        }
    }
}

// ---- pool 0x60 type 13: the player's hurt sparks, exe 0x566290 (gouraud lines 0x52, yellow -> red, additive) -------
void hurtSparks(DrawContext& c, const port::StaticActor& a, uint32_t object) {
    if (port::HurtSparksState(a.state) != port::HurtSparksState::Swing) return;
    const port::HurtSparksFields& f = port::classState<port::HurtSparksFields>(a);
    const port::HurtSparksVariant variant = port::hurtSparksVariants()[a.variant];
    const uint32_t col = std::min<uint32_t>((uint32_t(variant.colourScale) * f.ticks * 0x10) >> 8, 0xff);
    const int ext = variant.extraLength;
    float ctr[3];
    centre1616(a, ctr);
    const int age = int(f.startTicks) - int(f.ticks) + 1;
    for (uint32_t i = 0; i < f.count && i < 0x20; ++i) {
        const float ax = float(uint32_t(f.angles[i]) << 4), ay = float(uint32_t(f.heights[i]) << 4);
        const float len = float(age * int(f.radii[i]) * 0x10);
        Prim p;
        std::copy(ctr, ctr + 3, p.centre);
        p.object = object;
        p.screen = false;
        p.line = true;
        p.n = 2;
        p.blend = 2;
        float o[3];
        // exe 0x5e3970 returns R * (0, 0, d << 12) as 16.16: d / 16 units; (c, c, 0) at the outer end
        rotZ(ax, ay, (len + float(ext)) / 16.0f, o);
        p.v[0] = grad(0, 0, kRowHurt, 0, float(col) / 255.0f);
        std::copy(o, o + 3, p.v[0].off);
        rotZ(ax, ay, len / 16.0f, o); // (c, 0, 0)
        p.v[1] = grad(0, 0, kRowHurt, 1, float(col) / 255.0f);
        std::copy(o, o + 3, p.v[1].off);
        p.v[2] = p.v[0];
        p.v[3] = p.v[1];
        c.out.push_back(p);
    }
}

// ---- pool 0x60 type 0: the pickups, exe 0x562810's list entry -> exe 0x5f1500 ----------------------------------------
void pickup(DrawContext& c, const State& st, const port::StaticActor& a, uint32_t object) {
    const port::PickupFields& f = port::classState<port::PickupFields>(a);
    if (port::PickupState(a.state) < port::PickupState::Collected && !port::storyFlag(port::kFlagPickupsStay)) {
        const int16_t t = int16_t(f.timeout);
        if (t > 0 && t < port::kPickupBlinkTimeout && !(t & 1)) return; // blinking
    }
    const int kind = f.kind;
    // the colour index (the zenny's size, the energy's / the life's sprite) and the spin speed, which the draw list
    // takes as the model's scale
    const uint32_t colour = f.sprite, scale = uint8_t(f.spinSpeed);
    if (kind < 0 || kind > 2) return;
    float ctr[3];
    centre1616(a, ctr);
    ctr[1] += float(f.bob);
    if (kind == int(port::PickupKind::WeaponEnergy) || kind == int(port::PickupKind::Life)) { // the glow sprite
        const float size = kind == int(port::PickupKind::WeaponEnergy) ? float(scale >> 1) : float(scale);
        const int u0 = kind == int(port::PickupKind::WeaponEnergy) ? 0x40 : 0;
        billboard(c, object, ctr, size, (uint32_t(f.drawYaw) >> 1) & 0xfff,
                  c.tex.index(kGlowTpage, kPickupGlowClut, kPickupGlowTint), blendOfAbr(kGlowTpage >> 5), u0, 0x20, u0 + 0x1f,
                  0x3f);
    }
    // the model: vertices * Ry Rx (the drawn pitch, yaw) * scale / 16, gouraud colours cycling between the pair
    const PickupModel& model = pickupModels->models[size_t(kind)];
    if (!model.present) return;
    const float ax = float(int16_t(f.drawPitch)) * 6.2831853f / 4096.0f, ay = float(int16_t(f.drawYaw)) * 6.2831853f / 4096.0f;
    const float sx = std::sin(ax), cx = std::cos(ax), sy = std::sin(ay), cy = std::cos(ay), k = float(scale) / 16.0f;
    auto vert = [&](uint32_t i, float out[3]) {
        const std::array<int16_t, 3>& p = model.points.at(i);
        const float x = p[0], y = p[1], z = p[2];
        const float y1 = cx * y - sx * z, z1 = sx * y + cx * z; // Rx
        out[0] = (cy * x + sy * z1) * k;                         // Ry
        out[1] = y1 * k;
        out[2] = (-sy * x + cy * z1) * k;
    };
    const int row = kRowPickup + int(std::min<uint32_t>(colour, kPickupPairs - 1));
    auto colourT = [&](uint32_t t) { // exe 0x5f1500: t & 0x1f, a -> b over 16 steps, then back
        t &= 0x1f;
        return t < 16 ? float(t) / 16.0f : 1.0f - float(t - 16) / 16.0f;
    };
    auto emit = [&](std::initializer_list<uint32_t> idx, std::initializer_list<float> ts) {
        Prim p;
        std::copy(ctr, ctr + 3, p.centre);
        p.object = object;
        p.screen = false;
        p.blend = 1; // draw mode 0xe1000200: average
        p.n = int(idx.size());
        int j = 0;
        auto t = ts.begin();
        for (uint32_t i : idx) {
            float o[3];
            vert(i, o);
            p.v[j] = grad(0, 0, row, *t++, 1.0f);
            std::copy(o, o + 3, p.v[j].off);
            ++j;
        }
        c.out.push_back(p);
    };
    const uint32_t frame = st.tick;
    if (kind == int(port::PickupKind::Zenny) || kind == int(port::PickupKind::WeaponEnergy)) {
        int side = -2;
        for (uint32_t s = 0; s < 4; ++s, ++side) {
            float t[4];
            for (uint32_t n = 0; n < 4; ++n) t[n] = colourT(frame + s * 0x10 + n * 8);
            const uint32_t i = uint32_t(side - 1) & 3, j = uint32_t(side) & 3;
            emit({0, 1 + i, 1 + j}, {t[1], t[2], t[3]});
            if (kind == int(port::PickupKind::Zenny)) {
                emit({1 + i, 1 + j, 5 + i, 5 + j}, {t[0], t[1], t[2], t[3]});
                emit({5 + i, 5 + j, 9}, {t[0], t[1], t[2]});
            } else {
                emit({1 + i, 1 + j, 5}, {t[0], t[1], t[2]});
            }
        }
    } else { // the cube: two strips of 3 quads (the geometry unit's queue order of exe 0x5f1500)
        for (uint32_t s = 0; s < 2; ++s) {
            float t[4];
            for (uint32_t n = 0; n < 4; ++n) t[n] = colourT(frame + s * 0x10 + n * 8);
            const uint32_t b = s * 8;
            emit({b + 0, b + 1, b + 2, b + 3}, {t[0], t[1], t[2], t[3]});
            emit({b + 2, b + 3, b + 4, b + 5}, {t[3], t[0], t[1], t[2]});
            emit({b + 4, b + 5, b + 6, b + 7}, {t[2], t[3], t[0], t[1]});
        }
    }
}

// ---- ST1E: the extinguisher's stream (pool 0x10 type 14, exe 0x570a10) -----------------------------------------------
// per point i from the tail to the count: a billboard (code 0x2e, tpage 0x2f additive, clut 0x7c12, uv (u0, 0xe0)-(u0 +
// 0x1f, 0xff) with u0 = ((i + frame) << 5) & 0x60, size i * 10, colour 0x404030 - i * 0x040403) and a ring record disc
// (radius (i * 3 + 6) * 2, colour 0xc0c0b0 - i * 0x0c0c0b -> black)
void stream(DrawContext& c, const State& st, const port::ShotActor& a, uint32_t object) {
    const port::ExtinguisherState state = port::ExtinguisherState(a.state);
    if (port::isPartnerSlot(a) || state == port::ExtinguisherState::Spawn ||
        uint8_t(state) > uint8_t(port::ExtinguisherState::RunOut))
        return;
    const port::ExtinguisherFields& f = port::classState<port::ExtinguisherFields>(a);
    const uint32_t n = std::min<uint32_t>(f.pointCount, port::kStreamPoints);
    const int tex = c.tex.index(kGlowTpage, kStreamClut, kNeutralTint);
    for (uint32_t i = f.tail; i < n; ++i) {
        const auto pt = f.points[i];
        const float ctr[3] = {float(pt[0]), float(pt[1]), float(pt[2])};
        const int u0 = int(((i + st.tick) << 5) & 0x60);
        // the sprite colour 0x404030 - i * 0x40403 as a texel scale (neutral = 0x80)
        billboard(c, object | i, ctr, float(i * 10), 0, tex, blendOfAbr(kGlowTpage >> 5), u0, 0xe0, u0 + 0x1f, 0xff,
                  float(0x40 - int(i) * 4) / 128.0f);
        ring(c, object | i, ctr, 0.0f, float((i * 3 + 6) * 2), kRowStream, float(0xc0 - int(i) * 0xc) / 192.0f);
    }
}

// ---- the Shining Laser: pool 0xa0 global type 8 (exe 0x55fa10), its OT primitives -----------------------------------
// Reads the beam's block (game/shots/shining_laser): the level, the drawn steps, the counter, the muzzle, the aim (pitch,
// yaw), the 32 spark bytes. Points in projection units (/ 2^shift here); the frame counter 0x6d0006 is the draw tick.
// The ring records are draw commands (game/shots/shining_laser, decoded by game/draw_command_decode).
//  * exe 0x560310 (state Beam, the beam): through Ry Rx(aim) from the muzzle along -z, steps of 0x100:
//    band 1 -- per step i an 8-sided tube of radius R = level * 4 + 0x10 (a cone from the muzzle for i = 0), angles
//      (f & 7) + 8 j of the 64-step tables, GT4 tpage 0x2e (additive) CLUT 0x7d51, u 0xe0 .. 0xef, v scrolling
//      ((i << 5) - (f & 15) * 8) & 0x7f (- 0x60 from 0x60) .. + 0x1f, colour 0x808080;
//    band 2 -- every 4th step from f & 3: a twisted ring from radius R at z = -(s + 1) * 0x100 out to radius
//      level * 4 + 0x50 at z = -0x180 - s * 0x100 (angles + 4), CLUT 0x7d52 uv (0, 0x60) .. (0x1f, 0x7f), 0x404040;
//  * exe 0x55fc00 (states Start .. Beam, the muzzle) in the camera plane at the muzzle: 4 G4 spikes (4 wide, length
//    (rng & 0x7f) + 0x60 at the angle rng & 0xfff; 0x0c1808 / black tip / 0x303030 centre) -- the game draws them from
//    the RNG at draw time: the logic port (game/shots/shining_laser) keeps the values it drew (port::laserSpikes);
//    while charging the 32 sparks converging: radius r = (9 - counter) * A, angle B * 0x40 (the sparks' lengths A /
//    angles B), two G4 diamonds (black, 0x0c1808 at r -+ r / 4, 0x404040 at r, black). OpenDash draws them whatever the
//    PS1 camera's depth range says (the exe's view, not the one on screen).
void laserBeam(DrawContext& c, const State& st, const port::EffectActor& e, uint32_t object) {
    const port::BeamState state = port::BeamState(e.state);
    if (state != port::BeamState::Charge && state != port::BeamState::Beam) return;
    const port::LaserBeamFields& beam = port::classState<port::LaserBeamFields>(e);
    if (!beam.laserShot) return;
    const port::LaserBlock& block = port::classState<port::LaserBlock>(*beam.laserShot);
    const int sh = port::stage().coordinateShift & 0x1f;
    const float unit = 1.0f / float(1 << sh);
    const float mz[3] = {float(block.muzzle.x) * unit, float(block.muzzle.y) * unit, float(block.muzzle.z) * unit};
    const uint32_t f = st.tick;
    auto colourOf = [](Vertex& v, uint32_t col) {
        for (int q = 0; q < 3; ++q) v.color[q] = float((col >> (q * 8)) & 0xff) / 255.0f;
    };
    // the camera-plane parts (exe 0x55fc00)
    {
        auto dir12 = [&](uint32_t a, float r, Vertex& v) {
            a &= 0xfff;
            v.off[0] = float(fixedCos(int(a))) * r / 4096.0f;
            v.off[1] = float(fixedSin(int(a))) * r / 4096.0f;
        };
        auto quad = [&](uint32_t a0, float r0, uint32_t a1, float r1, uint32_t a2, float r2, uint32_t a3, float r3,
                        const uint32_t col[4]) {
            Prim p;
            std::copy(mz, mz + 3, p.centre);
            p.object = object;
            p.texture = kUntextured;
            p.blend = 2; // tpage 0x220: additive
            const uint32_t a[4] = {a0, a1, a2, a3};
            const float r[4] = {r0, r1, r2, r3};
            for (int k = 0; k < 4; ++k) {
                dir12(a[k], r[k], p.v[k]);
                colourOf(p.v[k], col[k]);
            }
            c.out.push_back(p);
        };
        uint16_t sp[8];
        if (port::laserSpikes(e.at, sp))
            for (int k = 0; k < 4; ++k) {
                const uint32_t a = sp[k * 2], len = sp[k * 2 + 1];
                static constexpr uint32_t kSpike[4] = {0x0c1808, 0, 0x303030, 0x0c1808};
                quad(a - 0x400, 4, a, float(len), 0, 0, a + 0x400, 4, kSpike);
            }
        const uint32_t n = block.counter;
        if (state == port::BeamState::Charge && n >= 2) {
            const uint32_t drawn = n - 1; // the counter the draw read (counted up after it)
            static constexpr uint32_t kSpark[4] = {0, 0x0c1808, 0x404040, 0};
            for (uint32_t k = 0; k < port::kLaserChargeSparks; ++k) {
                const uint32_t A = block.sparkLengths[k], B = block.sparkAngles[k];
                const int r = int(9 - drawn) * int(A);
                const float rr = float(r), in = float(r - (r >> 2)), out = float(r + (r >> 2));
                const uint32_t a = B * 0x40;
                quad(a - 0x10, rr, (B & 0x3f) * 0x40, in, (B & 0x3f) * 0x40, rr, a + 0x10, rr, kSpark);
                quad(a - 0x10, rr, (B & 0x3f) * 0x40, out, (B & 0x3f) * 0x40, rr, a + 0x10, rr, kSpark);
            }
        }
    }
    if (state != port::BeamState::Beam || block.beamOut == 0) return;
    // the beam (exe 0x560310): world quads through Ry(yaw) Rx(pitch)
    const float kA = 6.2831853f / 4096.0f;
    const float px = float(block.aim.x) * kA, py = float(block.aim.y) * kA;
    const float cx = std::cos(px), sx = std::sin(px), cy = std::cos(py), sy = std::sin(py);
    // R = Ry Rx (exe 0x40c070 with roll 0): Rx (x, y, z) then Ry
    auto world = [&](float x, float y, float z, Vertex& v) {
        const float y1 = cx * y - sx * z, z1 = sx * y + cx * z;
        const float o[3] = {cy * x + sy * z1, y1, -sy * x + cy * z1};
        for (int k = 0; k < 3; ++k) v.off[k] = o[k] * unit;
    };
    const uint32_t lvl = uint16_t(block.stats.level), steps = block.drawnSteps;
    const float R = float(lvl * 4 + 0x10), R2 = float(lvl * 4 + 0x50);
    const int t1 = c.tex.index(kLaserTpage, kLaserTubeClut), t2 = c.tex.index(kLaserTpage, kLaserRingClut);
    auto ringPt = [&](uint32_t idx, float r, float z, Vertex& v) {
        world(tableCos(idx) * r / 4096.0f, tableSin(idx) * r / 4096.0f, z, v);
    };
    const uint32_t f7 = f & 7;
    for (uint32_t i = 0; i < steps; ++i) {
        uint32_t v5 = ((i << 5) - (f & 0xf) * 8) & 0x7f;
        if (v5 > 0x5f) v5 -= 0x60;
        const float zn = -float(i) * 256.0f, zf = -float(i + 1) * 256.0f, rn = i == 0 ? 0.0f : R;
        for (uint32_t j = 0; j < 8; ++j) {
            const uint32_t i0 = (f7 + 8 * j + 0x38) & 0x3f, i1 = (f7 + 8 * j) & 0x3f;
            Prim p;
            std::copy(mz, mz + 3, p.centre);
            p.screen = false;
            p.object = object;
            p.texture = t1;
            p.blend = blendOfAbr(kLaserTpage >> 5);
            ringPt(i0, rn, zn, p.v[0]);
            ringPt(i0, R, zf, p.v[1]);
            ringPt(i1, rn, zn, p.v[2]);
            ringPt(i1, R, zf, p.v[3]);
            // The exe links the face only when the geometry unit's NCLIP of its last three projected points (far i0,
            // near i1, far i1: exe 0x40c7b0 -> 0x40cac0) is >= 0, i.e. it drops the tube's back faces as the GAME camera
            // sees them. OpenDash (not the game's): every face is drawn -- the view is the orbit camera, and from any
            // other angle the faces culled for the game camera left holes in the beam (2026-10-15 user report); the
            // beam is additive and already saturated, so the far side adds little.
            const float uu[4] = {0xe0, 0xe0, 0xef, 0xef}, vv[4] = {float(v5), float(v5 + 0x1f), float(v5), float(v5 + 0x1f)};
            for (int k = 0; k < 4; ++k) {
                p.v[k].u = uu[k] + 0.5f;
                p.v[k].v = vv[k] + 0.5f;
            }
            c.out.push_back(p);
        }
    }
    for (uint32_t s = f & 3; s < steps; s += 4) {
        const float zn = -float(s + 1) * 256.0f, zf = -0x180 - float(s) * 256.0f;
        for (uint32_t j = 0; j < 8; ++j) {
            const uint32_t n0 = (f7 + 8 * j + 0x38) & 0x3f, n1 = (f7 + 8 * j) & 0x3f;
            const uint32_t f0 = (f7 + 8 * j - 4) & 0x3f, f1 = (f7 + 8 * j + 4) & 0x3f;
            Prim p;
            std::copy(mz, mz + 3, p.centre);
            p.screen = false;
            p.object = object;
            p.texture = t2;
            p.blend = blendOfAbr(kLaserTpage >> 5);
            ringPt(n0, R, zn, p.v[0]);
            ringPt(f0, R2, zf, p.v[1]);
            ringPt(n1, R, zn, p.v[2]);
            ringPt(f1, R2, zf, p.v[3]);
            const float uu[4] = {0, 0, 0x1f, 0x1f}, vv[4] = {0x60, 0x7f, 0x60, 0x7f};
            for (int k = 0; k < 4; ++k) {
                p.v[k].u = uu[k] + 0.5f;
                p.v[k].v = vv[k] + 0.5f;
                p.v[k].shade = 0.5f; // 0x404040
            }
            c.out.push_back(p);
        }
    }
}

RoutineId handler(const port::ActorHeader& a, port::ActorPool p1, port::ActorPool p2) {
    const RoutineRef h = port::actorHandler(a, p1);
    return h ? h.id() : port::actorHandler(a, p2).id();
}
// a content class's draw by its routine (nullptr: none)
ClassDraw contentDraw(RoutineId h) {
    const auto& draws = port::content().effectDraws;
    const auto it = draws.find(h);
    return it == draws.end() ? nullptr : it->second;
}

} // namespace

uint8_t blendOfAbr(uint32_t abr) { return uint8_t((abr & 3) + 1); }

float tableCos(uint32_t i) { // 0xa72500 (x): cos
    i &= 0x3f;
    if (port::fixedCosMapped(int(i << 6))) return float(fixedCos(int(i << 6)));
    return std::cos(float(i) * 6.2831853f / 64.0f) * 4096.0f;
}
float tableSin(uint32_t i) { // 0xa6c660 (y): sin
    i &= 0x3f;
    if (port::fixedSinMapped(int(i << 6))) return float(fixedSin(int(i << 6)));
    return std::sin(float(i) * 6.2831853f / 64.0f) * 4096.0f;
}

void billboard(DrawContext& c, uint32_t object, const float centre[3], float size, uint32_t rot, int texture,
               uint8_t blend, int u0, int v0, int u1, int v1, float shade) {
    if (texture < 0) return;
    Prim p;
    std::copy(centre, centre + 3, p.centre);
    p.object = object;
    p.texture = texture;
    p.blend = blend;
    const uint32_t i = ((rot + 0x600) >> 6) & 0x3f;
    const float a = tableCos(i) * size / 4096.0f, b = tableSin(i) * size / 4096.0f;
    const float off[4][2] = {{-a, -b}, {-b, a}, {b, -a}, {a, b}};
    const float uv[4][2] = {{float(u0), float(v0)}, {float(u1), float(v0)}, {float(u0), float(v1)}, {float(u1), float(v1)}};
    for (int k = 0; k < 4; ++k) {
        p.v[k].off[0] = off[k][0];
        p.v[k].off[1] = off[k][1];
        p.v[k].u = uv[k][0] + 0.5f;
        p.v[k].v = uv[k][1] + 0.5f;
        p.v[k].shade = shade;
    }
    c.out.push_back(p);
}

Vertex grad(float x, float y, int row, float t, float shade) {
    Vertex v;
    v.off[0] = x;
    v.off[1] = y;
    v.u = 0.5f + std::clamp(t, 0.0f, 1.0f) * 255.0f;
    v.v = float(row) + 0.5f;
    v.shade = shade;
    return v;
}

void ring(DrawContext& c, uint32_t object, const float centre[3], float rIn, float rOut, int row, float shade) {
    if (rOut < 0) rOut = 0;
    if (rIn < 0) rIn = 0;
    for (int j = -3; j <= 12; ++j) {
        const uint32_t i0 = (uint32_t(j - 1) * 4) & 0x3f, i1 = (uint32_t(j) * 4) & 0x3f;
        Prim p;
        std::copy(centre, centre + 3, p.centre);
        p.object = object;
        p.blend = 2;
        const float s0 = tableCos(i0) / 4096.0f, c0 = tableSin(i0) / 4096.0f;
        const float s1 = tableCos(i1) / 4096.0f, c1 = tableSin(i1) / 4096.0f;
        p.v[0] = grad(s0 * rIn, c0 * rIn, row, 0, shade);
        p.v[1] = grad(s1 * rIn, c1 * rIn, row, 0, shade);
        p.v[2] = grad(s0 * rOut, c0 * rOut, row, 1, shade);
        p.v[3] = grad(s1 * rOut, c1 * rOut, row, 1, shade);
        c.out.push_back(p);
    }
}

int contentGradientRow(uint32_t inner, uint32_t outer) {
    const auto& rows = port::content().effectGradientRows;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i][0] == inner && rows[i][1] == outer) return kGameRows + int(i);
    return -1;
}

void addTexture(std::vector<Texture>& t, uint16_t tpage, uint16_t clut, uint32_t tint) {
    if (findTexture(t, tpage, clut, tint) < 0) t.push_back({tpage, clut, tint & 0xffffff});
}

int TextureCache::find(uint16_t tpage, uint16_t clut, uint32_t tint) const {
    return findTexture(list_, tpage, clut, tint);
}
int TextureCache::index(uint16_t tpage, uint16_t clut, uint32_t tint) {
    const int i = find(tpage, clut, tint);
    if (i >= 0) return i;
    list_.push_back({tpage, clut, tint & 0xffffff});
    dirty_ = true;
    return int(list_.size()) - 1;
}

std::vector<Texture> textures() {
    std::vector<Texture> t;
    addTexture(t, kExplosionTpage, kExplosionClut); // the death explosion (exe 0x5645b0)
    for (uint32_t v = 0; v < 10; ++v) {              // the hit spark's variants (exe 0x55d280: their styles)
        const port::SparkStyle& style = port::sparkStyles()[v];
        addTexture(t, style.tpage, style.clut);
    }
    for (uint32_t k = 0; k < 4; ++k) addTexture(t, kShotTpage, kShotClut, shotColours[k]); // sprite 1
    addTexture(t, kShotGlowTpage, kShotClut);                            // the shot's sprite 2
    addTexture(t, kGlowTpage, kPickupGlowClut, kPickupGlowTint);         // the pickups' glow sprites
    addTexture(t, kGlowTpage, kStreamClut);                              // ST1E: the extinguisher's stream
    for (const auto add : port::content().effectTextures) add(t);        // the content classes' (game/content_registry)
    return t;
}

std::vector<std::array<uint32_t, 2>> gradients() {
    std::vector<std::array<uint32_t, 2>> g;
    g.push_back({0xffffff, 0}); // kRowGrey
    for (uint32_t k = 0; k < 4; ++k) g.push_back({port::burstColours[k] & 0xffffff, 0}); // kRowBurst
    for (uint32_t k = 0; k < 4; ++k) g.push_back({shotColours[k] & 0xffffff, 0});       // kRowShot
    g.push_back({kHurtInner, kHurtOuter});                                              // kRowHurt
    for (uint32_t k = 0; k < kPickupPairs; ++k)                                        // kRowPickup
        g.push_back({pickupColours[k * 2] & 0xffffff, pickupColours[k * 2 + 1] & 0xffffff});
    for (const uint32_t col : kReticleColours) g.push_back({col, col}); // kRowReticle
    g.push_back({kStreamGlow, 0});                                       // kRowStream
    for (const auto& row : port::content().effectGradientRows) g.push_back(row); // the content classes' rows
    return g;
}

void collect(State& st, TextureCache& tex, std::vector<Prim>& out) {
    ++st.tick;
    DrawContext c{tex, out};
    port::ShotPool& shots = port::shotPool();
    for (uint32_t i = 0; i < shots.size(); ++i) { // pool 0x10
        const port::ShotActor& a = shots[i];
        if (!a.live()) continue;
        const RoutineId h = port::actorHandler(a, port::ActorPool::Shots).id();
        if (h == kBusterShotClass) shot(c, a, kShotObjects | i << 4);
        else if (h == kExtinguisherClass) stream(c, st, a, kStreamObjects | i << 5);
    }
    port::EffectPool& effects = port::effectPool();
    for (uint32_t i = 0; i < effects.size(); ++i) { // pool 0xa0
        const port::EffectActor& a = effects[i];
        if (!a.live()) continue;
        const RoutineId h = handler(a, port::ActorPool::Effects, port::ActorPool::EffectsLate);
        if (h == kHitSparkClass) spark(c, a, kEffectObjects | i << 4);
        else if (h == kBurstClass) burst(c, a, kEffectObjects | i << 4);
        else if (h == kBladeTrailClass) bladeTrail(c, st, a, kBladeTrailObjects | i << 4);
        else if (h == kLaserBeamClass) laserBeam(c, st, a, kLaserObjects | i << 4);
        else if (const ClassDraw draw = contentDraw(h)) draw(c, st, a.at, i);
    }
    // pool 0x40 (projectiles): their draw commands are written by the logic port (game/draw_commands); the ST1D
    // boulder's glow exe 0x5c7ca0 included
    std::map<uint32_t, std::array<int8_t, 4>> keep;
    port::StaticPool& statics = port::staticPool();
    for (uint32_t i = 0; i < statics.size(); ++i) { // pool 0x60
        const port::StaticActor& a = statics[i];
        if (!a.live()) continue;
        const RoutineId h = handler(a, port::ActorPool::Statics, port::ActorPool::StaticsLate);
        if (h == kPickupClass) pickup(c, st, a, kStaticObjects | i << 4);
        else if (h == kExplosionClass) {
            explosion(c, st, a, kStaticObjects | i << 4);
            keep[a.at] = st.flashes[a.at];
        } else if (h == kHurtSparksClass) hurtSparks(c, a, kStaticObjects | i << 4);
        else if (const ClassDraw draw = contentDraw(h)) draw(c, st, a.at, i);
    }
    st.flashes = std::move(keep);
    reticle(kRowReticle, out); // game/reticle_draw
}

} // namespace od::fx
