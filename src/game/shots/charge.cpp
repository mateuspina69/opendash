// The charge (weapon 10: no shot type of its own; the arm spawns its classes, game/player/player_weapons): its marker
// while the player charges (static global type 0xe, exe 0x566540: a pulsing sphere around the player's body, a ring
// and spiralling particles; the special weapon's energy spent while charging) and the shock wave it releases (static
// global type 0x17, exe 0x56aae0: a ring growing with its time on the ground, breaking the walls and hurting what it
// reaches; drawn as a ring of quads around its projected point and textured bands).
#include "engine/data_source.hpp"
#include "game/shots/charge_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace charge_state;

namespace {

inline constexpr const char* kChargeArea = "charge";

// ---- the charge's marker (static global type 0xe) ---------------------------------------------------------------------
// [0x8c4708]
enum class ChargeMarkerState : uint8_t { Start = 0, Charge = 1 }; // exe 0x566560 / 0x5665f0
constexpr int16_t kStartShrink = 3, kStartSoundTicks = 0x15, kHumTicks = 0xa;
constexpr uint16_t kChargeSound = 0xfb, kHumSound = 0x10b;
constexpr uint32_t kPhaseMask = 0x1ff;
// the sphere's colours (exe data 0x8c46e8: 8 RGB words; the variant >> 2 picks a pair, its low two bits the mix)
const DataTable<ScalarTable<uint32_t, 8>> chargeColours{TableId::ChargeColours};
constexpr uint32_t kSphereBands = 8, kBandQuads = 0x10;
constexpr uint32_t kSphereMixHigh = 0xe80000; // the colour word's code bits (0x3a, POLY_G4 semi) before the shift
constexpr int kParticleRadius = 120;
constexpr uint32_t kRingRgb = 0x202020, kParticleRgb = 0x604020;
constexpr uint32_t kSpiralLevel = 5; // from this charge level the particles' discs turn
constexpr int16_t kSpiralStep = 0x40;

// exe 0x5666a0(actor, b): around the player's body bone, turned by Ry(frame * 4): a sphere of kSphereBands bands x
// kBandQuads quads (additive POLY_G4, the faces turned to the camera only: the geometry unit's NCLIP > 0) of radius
// (4 - shrink) * 32, coloured between the pair of the variant and pulsing by the band; a ring record and the particles
// spiralling (ring records, their size by the charge level)
void chargeDraw(StaticActor& actor, ChargeMarkerFields& f) {
    const Player& p = player();
    const ShortVec3& body = p.bonePoints[kBodyBone];
    const int16_t org[3] = {body.x, body.y, body.z};
    const LocalFrame frame = localFrame(0, int16_t(uint16_t(uint32_t(gameState().frame) << 2)), 0, org);
    const int R0 = (4 - int(uint16_t(f.shrink))) << 5;
    const int sh = (0xc - int(stage().coordinateShift)) & 0x1f;
    const uint32_t vr = actor.variant;
    for (int i = 0; i < int(kSphereBands); ++i) {
        const uint32_t lo = uint32_t((i - 4) << 2) & 0x3f, hi = uint32_t((i - 3) << 2) & 0x3f;
        const int16_t yA = wrap16(fixedSin64(lo) * R0 >> sh), yB = wrap16(fixedSin64(hi) * R0 >> sh);
        const int rA = fixedCos64(lo) * R0 >> 12, rB = fixedCos64(hi) * R0 >> 12;
        const uint32_t ca = chargeColours[(vr >> 2) & 7], cb = chargeColours[((vr >> 2) + 1) & 7];
        const uint32_t mixB = (vr & 3) << 4, mixA = 0x40 - mixB;
        const uint32_t fr = uint8_t(gameState().frame) >> 1;
        auto pulse = [&](uint32_t k) {
            const int x = int((fr + k) & 0xf) - 8;
            return uint32_t(x < 0 ? -x : x);
        };
        const uint32_t m0 = pulse(uint32_t(i)), m1 = pulse(uint32_t(i) + 1);
        const uint32_t R = (cb & 0xff) * mixB + (ca & 0xff) * mixA, G = ((cb >> 8) & 0xff) * mixB + ((ca >> 8) & 0xff) * mixA,
                       B = ((cb >> 16) & 0xff) * mixB + ((ca >> 16) & 0xff) * mixA;
        const uint32_t c0 =
            ((((G * m0) & 0xfffffc03u) | ((R * m0) >> 8)) >> 2) | ((((B * m0) & 0xfffffc00u) | kSphereMixHigh) << 6);
        const uint32_t c1 = ((((G * m1) & 0xfffffc03u) | ((R * m1) >> 8)) >> 2) | (((B * m1) & 0xfffffc00u) << 6);
        int16_t pa[3], pb[3];
        frame.point(wrap16(fixedCos64(0) * rA >> sh), yA, wrap16(fixedSin64(0) * rA >> sh), pa);
        frame.point(wrap16(fixedCos64(0) * rB >> sh), yB, wrap16(fixedSin64(0) * rB >> sh), pb);
        CameraPoint sb = cameraPoint(pb);
        uint32_t ang = 0;
        for (int j = 0; j < int(kBandQuads); ++j) {
            ang = (ang + 4) & 0x3f;
            int16_t na[3], nb[3];
            frame.point(wrap16(fixedCos64(ang) * rA >> sh), yA, wrap16(fixedSin64(ang) * rA >> sh), na);
            frame.point(wrap16(fixedCos64(ang) * rB >> sh), yB, wrap16(fixedSin64(ang) * rB >> sh), nb);
            const CameraPoint s1 = cameraPoint(na), s2 = cameraPoint(nb);
            // exe 0x40c7b0 over the geometry unit's queue (previous B, new A, new B): the float screen points (here
            // the truncated ones) -- render output only
            const int64_t nclip = int64_t(s2.sy - sb.sy) * s1.sx + int64_t(sb.sy - s1.sy) * s2.sx +
                                  int64_t(s1.sy - s2.sy) * sb.sx;
            if (nclip > 0) {
                const uint32_t c[4] = {c0, c1, c0, c1};
                const int16_t v[4][3] = {{pa[0], pa[1], pa[2]}, {pb[0], pb[1], pb[2]}, {na[0], na[1], na[2]},
                                         {nb[0], nb[1], nb[2]}};
                worldG4(actor.at, c, v);
            }
            for (int k = 0; k < 3; ++k) {
                pa[k] = na[k];
                pb[k] = nb[k];
            }
            sb = s2;
        }
    }
    dc::Ring ring;
    ring.arc = dc::kRingFullArc | dc::ringAngle(uint32_t(uint8_t(gameState().frame)) & 0x3f);
    ring.inner = dc::ringColour(kRingRgb);
    ring.xy = dc::packXY(body.x, body.y);
    ring.zStep = dc::zWord(body.z) | dc::ringStep(1);
    ring.radii = dc::ringRadii(uint32_t(R0 - 4), uint32_t((R0 * 5) >> 2) & 0xffff);
    dc::emit(ring, actor.at);
    const uint32_t n = (p.charge & 0xf) + 1u;
    const int16_t bx = body.x, by = body.y, bz = body.z;
    for (uint32_t k = 0; k < kChargeParticles; ++k) {
        auto&& particle = f.particles[k];
        const int v = particle;
        const uint32_t a64 = uint32_t((v >> 6) + v) & 0x3f;
        const uint32_t i12 = uint32_t(v >> 1) & kAngleMask;
        const int sv = fixedSin(int(i12)), cv = fixedCos(int(i12));
        const int e = n >= kSpiralLevel ? (v >> 5) & 0x3f : 0;
        particle = int16_t(uint32_t(v + kSpiralStep) & kAngleMask);
        const int16_t x = wrap16(((fixedCos64(a64) * kParticleRadius >> 12) * sv >> 12) + bx);
        const int16_t z = wrap16(((fixedSin64(a64) * kParticleRadius >> 12) * sv >> 12) + bz);
        const int16_t y = wrap16((cv * kParticleRadius >> 12) + by);
        dc::Ring disc;
        disc.arc = dc::kRingFullArc | dc::ringAngle(uint32_t(e));
        disc.inner = dc::ringColour(kParticleRgb);
        disc.xy = dc::packXY(x, y);
        disc.zStep = dc::zWord(z) | dc::ringStep(4);
        disc.radii = dc::ringRadii(0, n);
        dc::emit(disc, actor.at);
    }
}
// exe 0x566560: kept through the warps, the phase, the shrink, the hum's ticks, the particles at random; the draw, the
// sound
void chargeStart(StaticActor& actor, ChargeMarkerFields& f) {
    actor.runFlags |= kActorWarpKept;
    f.phase = int16_t(gameState().frame & kPhaseMask);
    f.shrink = kStartShrink;
    f.soundTicks = kStartSoundTicks;
    for (uint32_t k = 0; k < kChargeParticles; ++k) f.particles[k] = int16_t(rngStep() & kAngleMask);
    chargeDraw(actor, f);
    soundPlayAtPlayer(kChargeSound); // exe 0x5d53e0
    actor.state += 1u;
}
// exe 0x5665f0: while the player charges: a special weapon below its free energy level spends its shot's cost every
// tick (short of it: freed); the hum every kHumTicks
void chargeTick(StaticActor& actor, ChargeMarkerFields& f) {
    f.phase = int16_t((uint32_t(uint8_t(f.phase)) + 1u) & 0xff);
    if (f.shrink != 0) --f.shrink;
    Player& p = player();
    if (p.charge == 0) {
        freeActor(actor);
        return;
    }
    const int weapon = p.activeSpecial;
    if (weaponLevel(p, weapon, WeaponStat::Energy) < kFreeEnergyLevel) {
        const int cost = p.shotCost;
        const int16_t left = wrap16(p.gauge - cost);
        p.gauge = left;
        if (left < cost) {
            freeActor(actor);
            return;
        }
    }
    chargeDraw(actor, f);
    --f.soundTicks;
    if (f.soundTicks == 0) {
        soundPlayAtPlayer(kHumSound);
        f.soundTicks = kHumTicks;
    }
}

// [0x8c48a4]
enum class ShockWaveState : uint8_t { Start = 0, Grow = 1 }; // exe 0x56ab00 / 0x56ab90
const DataTable<CollisionBoxRecord> waveBox{TableId::ChargeWaveBox};
constexpr uint16_t kWaveSound = 0x10c;
constexpr uint32_t kWaveShape = hitGroup(kHitGroupPlayerAttacks);
constexpr uint32_t kWaveAttack = kHitAttackByPlayer | kHitAttackStrongPush;
// the ring's quads (tpage 0x2f, CLUT 0x7c90, u 0xe0..0xff x v 0x20..0x3f) and the bands' texture (CLUT 0x7c90 / tpage
// 0x2f, cells of the strip by the band)
constexpr uint16_t kWaveTpage = 0x2f, kWaveClut = 0x7c90;
constexpr uint32_t kGreyScale = 0x10101;
constexpr uint32_t kWaveBands = 0x10;

// exe 0x56ae10(actor, b): at the projected width of its size: a ring of 16 FT4 (grey by the ticks) between w / 4 and
// 17 w / 16, and 16 textured bands (band records) of its size
void waveDraw(StaticActor& actor, const ShockWaveFields& f) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return;
    int16_t depth = 0;
    const int w = projectedWidth(P, f.size, &depth);
    if (w == 0) return;
    const uint32_t idx = gameState().frame & 3;
    const int rA = (w * 17) >> 4, rB = (w * 4) >> 4;
    const uint32_t S = dc::packXY(sxy[0], sxy[1]);
    const int t = f.ticks;
    const uint32_t c = t >= 0x10 ? 0x404040u : uint32_t(t << 2) * kGreyScale;
    const ScreenAnchor an = shiftedScreenAnchor(P);
    dc::TexturedQuad q;
    q.colour = dc::packetColour(dc::kPolyFT4Semi, c);
    q.clut = kWaveClut;
    q.tpage = kWaveTpage;
    q.uv[0] = dc::texel(0xe0, 0x3f);
    q.uv[1] = dc::texel(0xe0, 0x20);
    q.uv[2] = dc::texel(0xff, 0x3f);
    q.uv[3] = dc::texel(0xff, 0x20);
    uint32_t pa = screenAround(S, 60 + idx, rA), pb = screenAround(S, 60 + idx, rB);
    for (uint32_t k = 0; k < 0x10; ++k) {
        const uint32_t j = idx + k * 4;
        const uint32_t na = screenAround(S, j, rA), nb = screenAround(S, j, rB);
        const uint32_t xy[4] = {pa, pb, na, nb};
        anchoredFT4(actor.at, an, q, xy);
        pa = na;
        pb = nb;
    }
    const uint32_t c2 = c << 1;
    const uint32_t r = uint32_t(int32_t(f.size));
    for (uint32_t k = 0; k < kWaveBands; ++k) {
        // the arc word: the tilts e (low byte the inner ring's, the next the outer's) by the band, the angle offset by
        // the frame and the texture's repeat 2 (byte 3)
        const uint32_t e = 0x200 * (k + 1), d = 2 * k;
        dc::Band band;
        band.ring.arc = ((uint32_t(uint8_t(gameState().frame)) & 0x3f) | 0x200u) << 16 | e | d;
        band.ring.inner = dc::bandColour(c2);
        band.ring.xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole);
        band.ring.outer = dc::ringRim(c2);
        band.ring.zStep = dc::zWord(actor.pos.z.whole) | dc::ringStep(2);
        band.ring.radii = r << 16 | r;
        if (e < 0x1200) {
            const uint32_t lo = d & 3, hi = d & 0xfc;
            band.clutTexel = ((lo + 4) << 11) | (hi * 8 + 0x60) | uint32_t(kWaveClut) << 16;
            band.tpageTexel = (hi * 8 + 0x7f) | ((lo << 11) + 0x2f00) | uint32_t(kWaveTpage) << 16;
        } else {
            const uint32_t m = (0xf0 - d * 8) & 0x7e0, lo = d & 3;
            band.clutTexel = ((lo << 11) + 0x2f00) | (m + 0x60) | uint32_t(kWaveClut) << 16;
            band.tpageTexel = ((lo + 4) << 11) | (m + 0x7f) | uint32_t(kWaveTpage) << 16;
        }
        dc::emit(band, actor.at);
    }
}
// exe 0x56abf0: the walls (half its full size), then the hit entry (its size / 4, the attack scaled by the ticks left
// of its life)
void waveHit(const PlayerEnv& env, StaticActor& actor, ShockWaveFields& f) {
    probeBlastWalls(env, actor, f.fullSize >> 1);
    if (f.life == 0) notPorted(kChargeArea, "exe 0x56abf0: a wave of no life (the exe divides by zero)");
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint32_t attack = uint32_t(int(f.ticks) * int(f.attack) / int(f.life)) | kWaveAttack;
    const uint32_t shape = hitOwner(actor.slot) | uint32_t(int32_t(int16_t(f.size >> 2))) | kWaveShape;
    hitListSubmit(pos, f.hitBox, shape, attack); // exe 0x5e4120
}
// exe 0x56ab00: kept through the warps, its hit box cleared, its attack / size / life by its level, its box; the draw,
// the sound
void waveStart(StaticActor& actor, ShockWaveFields& f) {
    actor.runFlags |= kActorWarpKept;
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    const int lv = f.level;
    f.attack = uint16_t(lv * 2 + 4);
    const int16_t full = wrap16(lv * 12 + 0x80);
    f.fullSize = full;
    f.size = int16_t(full >> 2);
    f.life = int16_t(lv);
    f.ticks = int16_t(lv);
    actor.collisionBox.pointAt(waveBox.stateAddress());
    actor.actorHit = 0;
    waveDraw(actor, f);
    soundPlayAtPlayer(kWaveSound);
    actor.state += 1u;
}
// exe 0x56ab90: the size grows with the time; the hit; freed when its ticks run out
void waveGrow(const PlayerEnv& env, StaticActor& actor, ShockWaveFields& f) {
    --f.ticks;
    if (f.ticks == 0) {
        freeActor(actor);
        return;
    }
    f.size = wrap16(((f.life - f.ticks + 1) * f.fullSize) >> 2);
    waveHit(env, actor, f);
    waveDraw(actor, f);
}

} // namespace

void chargeMarkerUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<ChargeMarkerFields>(actor);
    switch (ChargeMarkerState(actor.state)) {
    case ChargeMarkerState::Start: chargeStart(actor, f); return;
    case ChargeMarkerState::Charge: chargeTick(actor, f); return;
    default: notPortedAt(kChargeArea, "0x566540 state", actor.state);
    }
}

void shockWaveUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<ShockWaveFields>(actor);
    switch (ShockWaveState(actor.state)) {
    case ShockWaveState::Start: waveStart(actor, f); return;
    case ShockWaveState::Grow: waveGrow(env, actor, f); return;
    default: notPortedAt(kChargeArea, "0x56aae0 state", actor.state);
    }
}

} // namespace od::port
