// The opening's effect class 0x435960 as the game draws it (presentation only: reads its state, never writes it).
// Every variant appends billboard records 0x2c (code 0x2e: semi-transparent, rotation 0 unless noted) at the draw
// command cursor from its update (exe 0x5f0580 draws them); they are reproduced here from the post-tick state with
// game/effects_draw's helpers. Positions are s16 game units; the chains step as exe 0x5e3750 does (s16 pos += (Ry(yaw)
// * v) >> 16, the exe's sine / cosine table).
#include "content/intro/opening_effects.hpp"

#include <cmath>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

constexpr uint32_t kOpeningObjects = 0x2c000u; // the prims' object ids: this base and the slot's index

// the textures: the engine flame (tpage 0x2e additive, CLUT 0x3c38), the streaks' cloud puffs (0x2e, 0x7c91), the
// exhaust glow (0x2f additive, 0x7c11, tint 0xc04040), the bone exhaust and the trail (0x2f, 0x7d90), the smoke puff
// (0x4e subtractive, 0x7c93: a sheet of 48 x 48 frames)
constexpr uint16_t kFlameTpage = 0x2e, kFlameClut = 0x3c38;
constexpr uint16_t kCloudTpage = 0x2e, kCloudClut = 0x7c91;
constexpr uint16_t kGlowTpage = 0x2f, kExhaustGlowClut = 0x7c11, kExhaustClut = 0x7d90;
constexpr uint32_t kExhaustGlowTint = 0xc04040;
constexpr uint16_t kSmokeTpage = 0x4e, kSmokeClut = 0x7c93;

// s16 p += (Ry(yaw) (x, y, z)) >> 16 (the 4096-step table; std:: without exe data)
void moveYaw(int16_t p[3], int yaw, int x, int y, int z) {
    const int i = yaw & 0xfff;
    int a, b;
    if (fixedSinMapped(i) && fixedCosMapped(i)) {
        a = fixedSin(i);
        b = fixedCos(i);
    } else {
        a = int(std::lround(std::sin(float(i) * 6.2831853f / 4096.0f) * 4096.0f));
        b = int(std::lround(std::cos(float(i) * 6.2831853f / 4096.0f) * 4096.0f));
    }
    const int64_t o[3] = {int64_t(b) * x + int64_t(a) * z, int64_t(0x1000) * y, -int64_t(a) * x + int64_t(b) * z};
    for (int k = 0; k < 3; ++k) p[k] = int16_t(uint16_t(p[k] + uint16_t(uint32_t(int32_t(o[k])) >> 16)));
}
// a prim centre (game units) from an s16 point
void centreOf(const int16_t p[3], float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = float(p[k]);
}
// ... from an s16 point of the game state
void centreOf(const ShortVec3& p, float out[3]) {
    out[0] = float(p.x);
    out[1] = float(p.y);
    out[2] = float(p.z);
}

// variant 4, exe 0x436080: two engines at R(yaw) * (+-0x1540, 0, 0x2600 + (frame & 3) * 32) / 16 from the owner; each
// a chain of 4 + 7 * 2 billboards stepping backwards along the yaw: k = 0: size (cos[(frame & 3) * 16] >> 10) + 0x100
// (-0x18 each), step size * 2, colour 0x3f (-2 each); k = 1..7: size (cos >> 10) - 16k + 0xb0 (-8 each), step size *
// 4, colour 0x3f - 8k (-4 each). uv (f, 0xa0)-(f + 0x1f, 0xbf), f = the frames[(frame - i - k) & 3]: blue jet glows.
void engineFlame(fx::DrawContext& c, const fx::State& st, const EffectActor& e, uint32_t object) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) return;
    const EngineFlameFields& f = classState<EngineFlameFields>(e);
    const int tex = c.tex.index(kFlameTpage, kFlameClut);
    const uint32_t frame = st.tick & 0xffff; // the frame counter 0x6d0006 (not kept by the logic port)
    const int yaw = f.yaw;
    const int wobble = int(int16_t(fx::tableCos((frame & 3) * 16))) >> 10;
    for (int engine = 0; engine < 2; ++engine) {
        int16_t p[3] = {e.pos.x, e.pos.y, e.pos.z};
        moveYaw(p, yaw, engine == 0 ? 0x1540 : -0x1540, 0, int(int16_t((frame & 3) << 5)) + 0x2600);
        for (int k = 0; k < 8; ++k) {
            int col = 0x3f - k * 8, size, step, n, dc;
            int16_t d[3] = {0, 0, 0};
            if (k == 0) {
                size = wobble + 0x100;
                moveYaw(d, yaw, 0, 0, size * 2);
                step = 0x18, n = 4, dc = 2;
            } else {
                size = wobble - k * 0x10 + 0xb0;
                moveYaw(d, yaw, 0, 0, size * 4);
                step = 8, n = 2, dc = 4;
            }
            for (int i = 0; i < n; ++i) {
                const int u = int(engineFlameFrames[(frame - uint32_t(i) - uint32_t(k)) & 3]);
                float ctr[3];
                centreOf(p, ctr);
                if (size > 0 && col > 0)
                    fx::billboard(c, object, ctr, float(size), 0, tex, fx::blendOfAbr(kFlameTpage >> 5), u, 0xa0, u + 0x1f, 0xbf,
                                  float(col) / 128.0f);
                col -= dc;
                size -= step;
                for (int q = 0; q < 3; ++q) p[q] = int16_t(p[q] + d[q]);
            }
        }
    }
}

// variants 5 / 6, exe 0x436340 / 0x436560: two chains of cloud puffs from the streak's position (pass 0: 8 puffs
// backwards, pass 1: 7 forwards from one step ahead), colour b * (8 - k) / 8 with b = t < 16 ? (side ? 2t : 16) :
// min(end - t, 16) (t = the age before the update counted it); uv (0x30 + f, 0)-(0x5f + f, 0x2f), f = the frames[((s8
// t >> 2) + k) & 3]. A streak's chain steps R * (0, 0, size * 8), a lifting streak's R * (0, s, s).
void cloudStreak(fx::DrawContext& c, EffectActor& e, uint32_t object, bool rise) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) return;
    const StreakFields f(e);
    const int t = f.age - 1, end = f.life;
    if (t < 0) return;
    const int len = f.length & 0xf;
    int b = 0x10;
    if (t < 0x10) {
        if (f.side != 0) b = t * 2;
    } else if (end - t <= 0xf) {
        b = end - t;
    }
    const size_t sel = size_t(int(f.speed));
    const DataTable<ScalarTable<uint32_t, 4>>& frames = rise ? liftStreakFrames : streakFrames;
    const int size0 = rise ? int(liftStreakKinds[sel].size) : int(streakKinds[sel].size);
    const int sizeStep = rise ? int(liftStreakKinds[sel].sizeStep) : int(streakKinds[sel].sizeStep);
    const int step0 = rise ? int(liftStreakKinds[sel].step) : 0, stepStep = rise ? int(liftStreakKinds[sel].stepStep) : 0;
    const int tex = c.tex.index(kCloudTpage, kCloudClut);
    const int yaw = f.yaw;
    const uint32_t phase = uint8_t(int8_t(uint8_t(t)) >> 2);
    for (int pass = 0; pass < 2; ++pass) {
        int16_t p[3] = {e.pos.x, e.pos.y, e.pos.z};
        int size = size0, step = step0, k = 0;
        if (pass == 1) {
            size -= sizeStep;
            k = 1;
            if (rise) {
                step -= stepStep;
                moveYaw(p, yaw, 0, -step, step);
            } else {
                moveYaw(p, yaw, 0, 0, size * 8);
            }
        }
        for (; k < 8; ++k) {
            const int col = ((8 - k) * b) >> 3;
            const int u = int(frames[(phase + uint32_t(k)) & 3]);
            float ctr[3];
            centreOf(p, ctr);
            if (size > 0 && col > 0)
                fx::billboard(c, object, ctr, float(size), 0, tex, fx::blendOfAbr(kCloudTpage >> 5), 0x30 + u, 0, 0x5f + u, 0x2f,
                              float(col) / 128.0f);
            size += len - sizeStep;
            if (rise) {
                step += len * 8 - stepStep;
                const int s = pass == 0 ? -step : step;
                moveYaw(p, yaw, 0, s, s);
            } else {
                moveYaw(p, yaw, 0, 0, pass == 0 ? -size * 8 : size * 8);
            }
        }
    }
}

// variant 7, exe 0x4367b0: one billboard (uv (0, 0x20)-(0x1f, 0x3f), size 0x60, rotation (t & 0x1f) << 7) and a ring
// record disc (kExhaustGlow at the centre -> black at radius 0x50, additive)
void exhaustGlowPuff(fx::DrawContext& c, const EffectActor& e, uint32_t object) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) return;
    const OwnerPuffFields& f = classState<OwnerPuffFields>(e);
    const int t = f.age - 1;
    if (t < 0) return;
    float ctr[3];
    centreOf(e.pos, ctr);
    fx::billboard(c, object, ctr, 96.0f, uint32_t(t & 0x1f) << 7, c.tex.index(kGlowTpage, kExhaustGlowClut, kExhaustGlowTint),
                  fx::blendOfAbr(kGlowTpage >> 5), 0, 0x20, 0x1f, 0x3f);
    fx::ring(c, object, ctr, 0.0f, 80.0f, fx::contentGradientRow(kExhaustGlow, 0), 1.0f);
}

// variant 8, exe 0x436900: billboards at the position (uv (0x20, 0x20)-(0x3f, 0x3f)): not thrown: 6 of colour (t << 6)
// / 20 + 8, size t * 24 / 20 (-3 each while > 3); thrown: 1 of colour (4 - t) * 16 / 4 + 0x20, size 2
void boneExhaustGlow(fx::DrawContext& c, const EffectActor& e, uint32_t object) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) return;
    const OwnerPuffFields& f = classState<OwnerPuffFields>(e);
    const int t = f.age - 1;
    if (t < 0) return;
    int n, col, size;
    if (f.thrown == 0) {
        n = 6, col = (t << 6) / 0x14 + 8, size = (t * 0x18) / 0x14;
    } else {
        n = 1, col = ((4 - t) * 0x10) / 4 + 0x20, size = 2;
    }
    const int tex = c.tex.index(kGlowTpage, kExhaustClut);
    float ctr[3];
    centreOf(e.pos, ctr);
    for (int i = 0; i < n; ++i) {
        if (size > 0 && col > 0)
            fx::billboard(c, object, ctr, float(size), 0, tex, fx::blendOfAbr(kGlowTpage >> 5), 0x20, 0x20, 0x3f, 0x3f,
                          float(col) / 128.0f);
        if (size > 3) size -= 3;
    }
}

// variant 9, exe 0x436ba0: 8 billboards (colour 0x404040, uv (0x20, 0x20)-(0x3f, 0x3f)), size 0x20 (-4 each), stepping
// by (growth * size) >> 1
void exhaustTrail(fx::DrawContext& c, const EffectActor& e, uint32_t object) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) return;
    const OwnerPuffFields& f = classState<OwnerPuffFields>(e);
    const int tex = c.tex.index(kGlowTpage, kExhaustClut);
    int16_t p[3] = {e.pos.x, e.pos.y, e.pos.z};
    const int growth[3] = {f.growth.x, f.growth.y, f.growth.z};
    int size = 0x20;
    for (int i = 0; i < 8; ++i) {
        float ctr[3];
        centreOf(p, ctr);
        fx::billboard(c, object, ctr, float(size), 0, tex, fx::blendOfAbr(kGlowTpage >> 5), 0x20, 0x20, 0x3f, 0x3f, 0.5f);
        for (int q = 0; q < 3; ++q) p[q] = int16_t(p[q] + int16_t((growth[q] * size) >> 1));
        size -= 4;
    }
}

// variant 10, exe 0x436d50: one billboard, frame f = t / lifeParam of the smoke sheet (u (f & 3) * 0x30, v f < 4 ? 0 :
// 0x30), colour (end - t) * 0x80 / end, its size, rotation (t & 0x3ff) << 2
void smokePuff(fx::DrawContext& c, const EffectActor& e, uint32_t object) {
    const ThrownPuffFields& f = classState<ThrownPuffFields>(e);
    if (OpeningEffectState(e.state) == OpeningEffectState::Start || f.lifeParam == 0) return;
    const int t = int(uint16_t(f.age)) - 1, end = uint16_t(f.life);
    if (t < 0 || end == 0) return;
    const uint32_t frame = uint32_t(t / int(f.lifeParam));
    const int u = int(frame & 3) * 0x30, v = frame < 4 ? 0 : 0x30;
    const int col = ((end - t) * 0x80) / end;
    float ctr[3];
    centreOf(e.pos, ctr);
    if (col > 0)
        fx::billboard(c, object, ctr, float(uint16_t(f.size)), uint32_t(t & 0x3ff) << 2, c.tex.index(kSmokeTpage, kSmokeClut),
                      fx::blendOfAbr(kSmokeTpage >> 5), u, v, u + 0x2f, v + 0x2f, float(col) / 128.0f);
}

} // namespace

void openingEffectDraw(fx::DrawContext& c, const fx::State& st, uint32_t actor, uint32_t slot) {
    EffectActor& e = effectActorAt(actor);
    const uint32_t object = kOpeningObjects | slot << 4;
    switch (OpeningEffect(e.variant)) {
    case OpeningEffect::EngineFlame: engineFlame(c, st, e, object); break;
    case OpeningEffect::Streak: cloudStreak(c, e, object, false); break;
    case OpeningEffect::LiftStreak: cloudStreak(c, e, object, true); break;
    case OpeningEffect::OwnerPuff: exhaustGlowPuff(c, e, object); break;
    case OpeningEffect::BonePuff: boneExhaustGlow(c, e, object); break;
    case OpeningEffect::BurstPuff: exhaustTrail(c, e, object); break;
    case OpeningEffect::ThrownPuff: smokePuff(c, e, object); break;
    default: break; // FlashRing (exe 0x436f00): a screen-space G3 ring linked straight into the OT: not drawn here
    }
}

void openingEffectTextures(std::vector<fx::Texture>& out) {
    fx::addTexture(out, kFlameTpage, kFlameClut);
    fx::addTexture(out, kCloudTpage, kCloudClut);
    fx::addTexture(out, kGlowTpage, kExhaustGlowClut, kExhaustGlowTint);
    fx::addTexture(out, kGlowTpage, kExhaustClut);
    fx::addTexture(out, kSmokeTpage, kSmokeClut);
}

} // namespace od::port
