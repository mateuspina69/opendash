// The crusher (weapon 3, pool 0x10 type 2, exe 0x56c6a0): an energy orb (static global type 0x10, exe 0x567080) held at
// the hand for 4 ticks, then thrown in an arc along the player's aim; a hit or a wall bursts the orb, a fall frees both.
// The burst orb swells and shrinks for its weapon's time, breaking the walls and hurting what it reaches; drawn as
// crackling rings around its projected point (ordering-table polygons) and three textured rings turning around it.
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/shots/crusher_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace crusher_state;

namespace {

inline constexpr const char* kCrusherArea = "crusher";

// ---- the energy orb (static global type 0x10) ------------------------------------------------------------------------
// [0x8c4724]
enum class OrbState : uint8_t {
    Start = 0, // exe 0x5670a0
    Held = 1,  // exe 0x567120: carried by the shot until it bursts
    Burst = 2, // exe 0x5671f0
};
const DataTable<CollisionBoxRecord> orbBox{TableId::CrusherOrbBox};
constexpr int16_t kOrbIdleWidth = 0x50;
constexpr int16_t kOrbSizePerLevel = 0x60, kOrbBaseSize = 0xe0;
constexpr uint32_t kOrbBaseTicks = 0x20; // << the weapon's extra level
constexpr int16_t kOrbSoundTicks = 0x15, kOrbHumTicks = 0xa;
constexpr uint16_t kOrbBurstSound = 0x105, kOrbHumSound = 0x106, kOrbEndSound = 0x107;
constexpr int16_t kRingTurn = 0xb0;
constexpr int16_t kQuarterTilt = kQuarterTurn; // the second / third ring's start tilt
constexpr uint32_t kOrbShape = hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint;
constexpr uint32_t kOrbAttack = kHitAttackByPlayer | kHitAttackStrongPush;
// the polygons: the crackle strip (CLUT 0x7d53, u 0x80..0x9f x v 0xa0..0xbf) and the ring strip (CLUT 0x7d90,
// u / v 0xa0..0xbf) of tpage 0x2f
constexpr uint16_t kOrbTpage = 0x2f, kCrackleClut = 0x7d53, kRingClut = 0x7d90;
constexpr uint32_t kGreyScale = 0x10101;
constexpr uint32_t kIdleGrey = 0x808080;
constexpr uint32_t kRingSteps = 0x10; // the 64-step circle by 4

// the crackle strip's GT4 of four colours
dc::GouraudTexturedQuad crackleQuad(uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
    dc::GouraudTexturedQuad q;
    q.colour[0] = c0;
    q.colour[1] = c1;
    q.colour[2] = c2;
    q.colour[3] = c3;
    q.clut = kCrackleClut;
    q.tpage = kOrbTpage;
    q.uv[0] = dc::texel(0x80, 0xa0);
    q.uv[1] = dc::texel(0x9f, 0xa0);
    q.uv[2] = dc::texel(0x80, 0xbf);
    q.uv[3] = dc::texel(0x9f, 0xbf);
    return q;
}
// the ring strip's texels (u / v 0xa0..0xbf)
void ringTexels(uint16_t uv[4]) {
    uv[0] = dc::texel(0xa0, 0xbf);
    uv[1] = dc::texel(0xa0, 0xa0);
    uv[2] = dc::texel(0xbf, 0xbf);
    uv[3] = dc::texel(0xbf, 0xa0);
}

// the two jagged rings around a projected point (exe 0x5674e0 / the tail of 0x567780): 2 x 8 crackle GT4, radii
// (k + 1..8) w / 16 inside and (k + 5..12) w / 16 outside (random), the last quad closing on the base radii
void jaggedRings(uint32_t owner, const ScreenAnchor& an, uint32_t sxy, int w, uint32_t col) {
    int rInBase = w * 4, rOutBase = w * 8;
    for (int k = 0; k < 2; ++k) {
        uint32_t e = (uint32_t(gameState().frame) - uint32_t(k * 2)) & 3;
        const int rIn = rInBase >> 4, rOut = rOutBase >> 4;
        uint32_t p0 = screenAround(sxy, e, rIn), p1 = screenAround(sxy, e, rOut);
        for (int j = 0; j < 8; ++j) {
            e = (e + 8) & 0x3f;
            int ri, ro;
            if (j != 7) {
                const uint32_t r = rngStep(), rr = rngStep();
                ri = int((uint32_t((r & 7) + uint32_t(k) + 1) * uint32_t(w)) >> 4);
                ro = int((uint32_t((rr & 7) + uint32_t(k) + 5) * uint32_t(w)) >> 4);
            } else {
                ri = rIn;
                ro = rOut;
            }
            const uint32_t q0 = screenAround(sxy, e, ri), q1 = screenAround(sxy, e, ro);
            const uint32_t xy[4] = {p0, p1, q0, q1};
            anchoredGT4(owner, an, crackleQuad(col, col, col, col), xy);
            p0 = q0;
            p1 = q1;
        }
        rInBase += w;
        rOutBase += w;
    }
}
// exe 0x5674e0: the held orb -- the jagged rings (grey) at the projected width of kOrbIdleWidth
void orbIdleDraw(StaticActor& actor) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    projectOnScreen(P, sxy); // (its result is not tested)
    int16_t depth = 0;
    const int w = projectedWidth(P, kOrbIdleWidth, &depth);
    if (w == 0) return;
    jaggedRings(actor.at, shiftedScreenAnchor(P), dc::packXY(sxy[0], sxy[1]), w,
                dc::packetColour(dc::kPolyGT4Semi, kIdleGrey));
}
// exe 0x567780(actor, b): at the projected width of its size: two screen discs, 4 crackling bolts of 3 GT4 each going
// out from the centre, the jagged rings, a ring of 16 FT4 between w / 2 and 14 w / 16
void orbDraw(StaticActor& actor, const EnergyOrbFields& f) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return;
    int16_t depth = 0;
    const int w = projectedWidth(P, f.size, &depth);
    if (w == 0) return;
    int16_t zz = wrap16(depth - w);
    if (zz < 1) zz = 1;
    const int t = f.ticks;
    const int c = t >= 0x10 ? 0xff : t << 4;
    const uint32_t S = dc::packXY(sxy[0], sxy[1]);
    dc::Ring disc;
    disc.inner = dc::screenRingColour(uint32_t(c) * kGreyScale);
    disc.xy = S;
    disc.zStep = dc::zWord(zz) | dc::ringStep(2);
    disc.radii = (uint32_t(w * 3) << 14) & 0x3fff0000u; // the outer radius 3 w / 4
    dc::emit(disc, actor.at);
    int c2 = c * 4;
    if (c2 > 0xff) c2 = 0xff;
    disc.inner = dc::screenRingColour(uint32_t(c2) * kGreyScale);
    disc.radii = (uint32_t(w) & 0xfff8u) << 13; // the outer radius w / 8
    dc::emit(disc, actor.at);
    const ScreenAnchor an = shiftedScreenAnchor(P);
    auto pm = [](uint32_t cc, uint32_t i, int r, int sign) {
        const uint32_t x = uint16_t(wrap16(int16_t(uint16_t(cc)) + sign * (fixedCos64(i) * r >> 12)));
        const uint32_t y = uint16_t(wrap16(int16_t(uint16_t(cc >> 16)) + sign * (fixedSin64(i) * r >> 12)));
        return y << 16 | x;
    };
    for (uint32_t m = 0; m < 0x40; m += 0x10) {
        uint32_t base = ((rngStep() & 0xf) + m) & 0x3f;
        const int d = int((((rngStep() & 7) + 8) * uint32_t(w)) >> 7);
        uint32_t C = S;
        uint32_t e = (base - 0x10) & 0x3f;
        uint32_t qp = pm(C, e, d, 1), qm = pm(C, e, d, -1);
        int acc = c << 6;
        const int dec = -(c << 5);
        for (int n = 3; n > 0; --n) {
            const int dd = int((((rngStep() & 0x1f) + 0x30) * uint32_t(w)) >> 7);
            C = screenAround(C, base, dd);
            const uint32_t cc = uint32_t(((n << 5) * c) >> 8);
            const uint32_t col0 = dc::packetColour(dc::kPolyGT4Semi, cc * kGreyScale);
            e = (base - 0x10) & 0x3f;
            const int ee = int((((rngStep() & 7) + 8) * uint32_t(w)) >> 7);
            const uint32_t np = pm(C, e, ee, 1), nm = pm(C, e, ee, -1);
            const uint32_t cc2 = uint32_t(acc >> 8);
            const uint32_t col1 = dc::packetColour(dc::kPolyGT4Semi, cc2 * kGreyScale);
            const uint32_t xy[4] = {qp, qm, np, nm};
            anchoredGT4(actor.at, an, crackleQuad(col0, col0, col1, col1), xy);
            int s = int(rngStep() & 0xf) - 8;
            if (s >= 0) ++s;
            base = uint32_t(((n + 1) * s >> 2) + int(base)) & 0x3f;
            acc += dec;
            qp = np;
            qm = nm;
        }
    }
    jaggedRings(actor.at, an, S, w, dc::packetColour(dc::kPolyGT4Semi, uint32_t(c >> 2) * kGreyScale));
    const uint32_t idx = gameState().frame & 3;
    const int rA = (w * 8) >> 4, rB = (w * 14) >> 4;
    dc::TexturedQuad ring;
    ring.colour = dc::packetColour(dc::kPolyFT4Semi, uint32_t(c >> 1) * kGreyScale);
    ring.clut = kRingClut;
    ring.tpage = kOrbTpage;
    ringTexels(ring.uv);
    uint32_t pa = screenAround(S, 60 + idx, rA), pb = screenAround(S, 60 + idx, rB);
    for (uint32_t m = 0; m < kRingSteps; ++m) {
        const uint32_t j = idx + m * 4;
        const uint32_t na = screenAround(S, j, rA), nb = screenAround(S, j, rB);
        const uint32_t xy[4] = {pa, pb, na, nb};
        anchoredFT4(actor.at, an, ring, xy);
        pa = na;
        pb = nb;
    }
}
// exe 0x567f10(actor, b): three textured rings (16 GT4 each) of radius size / 2 + i * 16 .. size + i * 32 around the
// orb in the planes of its rings' angles, grey by the ticks
void orbRings(StaticActor& actor, const EnergyOrbFields& f) {
    const int16_t org[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    for (uint32_t i = 0; i < kOrbRings; ++i) {
        const ShortVec3& angles = f.rings[i];
        const LocalFrame frame = localFrame(angles.x, angles.y, angles.z, org);
        const int rad = f.size + int(i) * 0x20, rin = rad >> 1;
        const uint32_t idx = gameState().frame & 3;
        const int t = f.ticks;
        const uint32_t cv = t >= 0x10 ? 0x80u : uint32_t(t << 3);
        const uint32_t col = dc::packetColour(dc::kPolyGT4Semi, cv * kGreyScale);
        dc::GouraudTexturedQuad q;
        q.colour[0] = q.colour[1] = q.colour[2] = q.colour[3] = col;
        q.clut = kRingClut;
        q.tpage = kOrbTpage;
        ringTexels(q.uv);
        int16_t pi[3], po[3];
        frame.point(wrap16(fixedCos64(60 + idx) * rin >> 12), wrap16(fixedSin64(60 + idx) * rin >> 12), 0, pi);
        frame.point(wrap16(fixedCos64(60 + idx) * rad >> 12), wrap16(fixedSin64(60 + idx) * rad >> 12), 0, po);
        for (uint32_t k = 0; k < kRingSteps; ++k) {
            const uint32_t ang = idx + k * 4;
            int16_t ni[3], no[3];
            frame.point(wrap16(fixedCos64(ang) * rin >> 12), wrap16(fixedSin64(ang) * rin >> 12), 0, ni);
            frame.point(wrap16(fixedCos64(ang) * rad >> 12), wrap16(fixedSin64(ang) * rad >> 12), 0, no);
            const int16_t v[4][3] = {{pi[0], pi[1], pi[2]}, {po[0], po[1], po[2]}, {ni[0], ni[1], ni[2]},
                                     {no[0], no[1], no[2]}};
            worldGT4(actor.at, q, v);
            for (int a = 0; a < 3; ++a) {
                pi[a] = ni[a];
                po[a] = no[a];
            }
        }
    }
}
// exe 0x5672c0(actor, b): the walls (half its size), then the hit entry (its size / 4, the attack scaled by the ticks
// left of its life)
void orbHit(const PlayerEnv& env, StaticActor& actor, EnergyOrbFields& f) {
    probeBlastWalls(env, actor, f.size >> 1);
    if (f.life == 0) notPorted(kCrusherArea, "exe 0x5672c0: an orb of no life (the exe divides by zero)");
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint32_t attack = uint32_t(int(f.ticks) * int(f.attack) / int(f.life)) | kOrbAttack;
    const uint32_t shape = hitOwner(actor.slot) | uint32_t(int32_t(int16_t(f.size >> 2))) | kOrbShape;
    hitListSubmit(pos, f.hitBox, shape, attack); // exe 0x5e4120
}
// exe 0x5670a0: kept through the warps, its hit box cleared, the weapon in use's attack and level, its box
void orbStart(StaticActor& actor, EnergyOrbFields& f) {
    actor.runFlags |= kActorWarpKept;
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    const Player& p = player();
    const int w = p.activeSpecial;
    f.attack = uint16_t(weaponAttack(p, w));
    f.level = int16_t(weaponLevel(p, w, WeaponStat::Attack));
    actor.collisionBox.pointAt(orbBox.stateAddress());
    actor.actorHit = 0;
    orbIdleDraw(actor);
    actor.state += 1u;
}
// exe 0x567120: waits for the burst (set by the shot), then: its ticks kOrbBaseTicks << the weapon's extra level, its
// size by the level, the rings' tilts, the sound
void orbHeld(StaticActor& actor, EnergyOrbFields& f) {
    if (f.burst == 0) {
        orbIdleDraw(actor);
        return;
    }
    const Player& p = player();
    const uint32_t ticks = kOrbBaseTicks << (uint8_t(weaponLevel(p, p.activeSpecial, WeaponStat::Extra)) & 0x1f);
    f.soundTicks = kOrbSoundTicks;
    f.life = int16_t(ticks);
    f.ticks = int16_t(ticks);
    f.fullSize = int16_t(uint16_t(uint32_t(wrap16(f.level * kOrbSizePerLevel)) + uint32_t(kOrbBaseSize)));
    f.size = int16_t((f.ticks * 3) >> 2);
    for (uint32_t i = 0; i < kOrbRings; ++i) {
        auto&& ring = f.rings[i];
        ring.x = i == 1 ? kQuarterTilt : int16_t(0);
        ring.y = i == 2 ? kQuarterTilt : int16_t(0);
        ring.z = 0;
    }
    orbDraw(actor, f);
    orbRings(actor, f);
    soundPlayAtPos(kOrbBurstSound, actor.pos);
    actor.state += 1u;
}
// exe 0x5671f0: the rings turn, the size swells then shrinks with the ticks; the hit; the hum every kOrbHumTicks, the
// end sound and freed when its ticks run out
void orbBurst(const PlayerEnv& env, StaticActor& actor, EnergyOrbFields& f) {
    --f.ticks;
    if (f.ticks == 0) {
        soundPlayAtPos(kOrbEndSound, actor.pos);
        freeActor(actor);
        return;
    }
    for (uint32_t i = 0; i < kOrbRings; ++i) {
        auto&& ring = f.rings[i];
        ring.x += uint32_t(kRingTurn);
        ring.y += uint32_t(kRingTurn);
    }
    const int c = f.life, t = f.ticks;
    const int d = c - t;
    if (d > 2) f.size = wrap16(((t + c) >> 1) * f.fullSize / c);
    else f.size = wrap16((f.fullSize * (0xb - d)) >> 3);
    orbHit(env, actor, f);
    orbDraw(actor, f);
    orbRings(actor, f);
    --f.soundTicks;
    if (f.soundTicks == 0) {
        soundPlayAtPos(kOrbHumSound, actor.pos);
        f.soundTicks = kOrbHumTicks;
    }
}

// [0x8c494c]
enum class CrusherState : uint8_t { Spawn = 0, Hold = 1, Thrown = 2 }; // exe 0x56c6e0 / 0x56c910 / 0x56c980

const DataTable<CollisionBoxRecord> crusherBox{TableId::CrusherBox};
constexpr uint16_t kHoldTicks = 4;
constexpr uint16_t kThrowSound = 0x104;
constexpr int kThrowUp = -0x100, kThrowForward = -0x600;
constexpr int16_t kThrowGravity = 0x30;
constexpr uint16_t kThrowSpin = 0x100;
constexpr int16_t kFallLimit = 0x2000; // below this height both are freed
constexpr uint32_t kCrusherAttack = 0x80;

// the shot's position at the player's hand (<< 16)
void placeAtHandWhole(ShotActor& shot) {
    const ShortVec3& hand = player().bonePoints[kLeftHandBone];
    shot.pos.x = int32_t(uint32_t(int32_t(hand.x)) << 16);
    shot.pos.y = int32_t(uint32_t(int32_t(hand.y)) << 16);
    shot.pos.z = int32_t(uint32_t(int32_t(hand.z)) << 16);
}
// the orb at the shot
void carryOrb(const ShotActor& shot, const CrusherFields& fields) { fields.orb->pos = shot.pos; }
// the energy orb the shot carries bursts
void burstOrb(const CrusherFields& fields) { classState<EnergyOrbFields>(*fields.orb).burst = 1; }

void spawn(const PlayerEnv& env, ShotActor& shot, CrusherFields& fields) { // exe 0x56c6e0
    const Player& p = player();
    shot.flags |= kActorDrawn | kActorMeshModel;
    shot.runFlags |= kActorShown | kActorMeshModel | kActorWarpKept;
    const uint32_t o = bindActorModel(shot, 0) ? allocActor60(0) : 0; // exe 0x5e0ba0, exe 0x5e1300
    if (o == 0) {
        freeActor(shot);
        return;
    }
    StaticActor& orb = staticActorAt(o);
    orb.flags = kActorLive;
    orb.type = typeByte(GlobalStaticType::EnergyOrb);
    orb.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    orb.variant = 0;
    classState<EnergyOrbFields>(orb).clearParams();
    fields.orb = &orb;
    PlayerShotFields(shot).hitBox.result = 0;
    placeAtHandWhole(shot);
    shot.rotation.x = 0;
    shot.rotation.z = 0;
    shot.rotation.y = p.rotation.y;
    fields.throwYaw = uint16_t(p.rotation.y);
    int32_t v[3];
    playerAimVector(0, kThrowUp, kThrowForward, v); // exe 0x5e3e50
    if (p.onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = wrap16(v[2]);
    } else {
        int x, z;
        platformMove(x, z);
        shot.velocity.x = wrap16(x);
        shot.velocity.z = wrap16(z + v[2]);
    }
    shot.velocity.y = wrap16(v[1]);
    // the previous position (copied from the stale position by the dispatcher) one throw step behind
    moveAlongYaw(shot.prevPos, int16_t(fields.throwYaw), 0, 0, -int(shot.velocity.z) * 2); // exe 0x5e36f0
    carryOrb(shot, fields);
    shot.airborne = 0;
    shot.onPlatform = 0;
    shot.collisionBox.pointAt(crusherBox.stateAddress());
    shot.actorHit = 0;
    actorPushOut(env, shot); // exe 0x410960
    paySpecialEnergy(player());
    fields.holdTicks = kHoldTicks;
    ++shot.state;
}

void hold(ShotActor& shot, CrusherFields& fields) { // exe 0x56c910
    placeAtHandWhole(shot);
    carryOrb(shot, fields);
    --fields.holdTicks;
    if (fields.holdTicks != 0) return;
    soundPlayAtPlayer(kThrowSound);
    ++shot.state;
}

void thrown(const PlayerEnv& env, ShotActor& shot, CrusherFields& fields) { // exe 0x56c980: spinning, falling
    shot.rotation.x = wrap16(shot.rotation.x + kThrowSpin);
    if (shot.actorHit != 0 || (PlayerShotFields(shot).hitBox.result & (kHitAttackSolid | kHitAttackDeflects))) {
        burstOrb(fields);
        freeActor(shot);
        return;
    }
    shot.velocity.y = wrap16(shot.velocity.y + kThrowGravity);
    moveAlongYaw(shot.pos, int16_t(fields.throwYaw), shot.velocity.x, shot.velocity.y, shot.velocity.z);
    carryOrb(shot, fields);
    if (int32_t(actorTestMove(env, shot)) < 0) {
        actorPushOut(env, shot);
        burstOrb(fields);
        freeActor(shot);
        return;
    }
    if (shot.pos.y.whole > kFallLimit) {
        freeActor(shot);
        freeActor(*fields.orb);
        return;
    }
    actorCollisionSubmitMover(shot); // exe 0x4140b0
    submitShotHit(shot, hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x20),
                  kHitAttackByPlayer | kCrusherAttack);
    markFlying(shot);
}

} // namespace

void energyOrbUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<EnergyOrbFields>(actor);
    switch (OrbState(actor.state)) {
    case OrbState::Start: orbStart(actor, f); return;
    case OrbState::Held: orbHeld(actor, f); return;
    case OrbState::Burst: orbBurst(env, actor, f); return;
    default: notPortedAt(kCrusherArea, "0x567080 state", actor.state);
    }
}

void crusherShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<CrusherFields>(shot);
    shot.savePreviousPose(); // exe 0x56c6a0 copies before the state
    switch (CrusherState(shot.state)) {
    case CrusherState::Spawn: spawn(env, shot, fields); return;
    case CrusherState::Hold: hold(shot, fields); return;
    case CrusherState::Thrown: thrown(env, shot, fields); return;
    default: notPortedAt(kCrusherArea, "0x56c6a0 state", shot.state);
    }
}

} // namespace od::port
