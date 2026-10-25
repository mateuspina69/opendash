// The snow reaverbot class 0x4a2a50 (see snow_reaverbot.hpp). States by sub-type [[0x893c6c + sub * 4] + state * 4]:
// sub 0 0x4a2ad0 / 0x4a2bd0 / 0x468fa0, sub 1 0x4a3d20 / 0x4a3e10 / 0x468fa0, sub 4 (0x893c60) 0x4a4230 / 0x4a4330 /
// 0x468fa0; the behaviour (+9) by sub-type: sub 0 [0x893c80], sub 1 [0x893cb4], sub 4 [0x893cbc]. Its animations are
// model-bank indices keyed by the behaviour.
#include "game/actor_state.hpp"
#include "game/snow_reaverbot_state.hpp"
#include "game/snow_reaverbot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/room_records.hpp"
#include "game/shadows.hpp"

namespace od::port {

using namespace snow_reaverbot_state;

namespace {

constexpr const char* kSnowPort = "snow reaverbot port"; // the area of its port-gap errors

// +0x157
constexpr uint8_t kSnowAirborne = 0x01, kSnowInactive = 0x02, kSnowRunVariant = 0x04, kSnowStagger = 0x08;
constexpr uint8_t kSnowRetreated = 0x10, kSnowHit = 0x20;
constexpr int8_t kSnowPlacedAwake = 2;

enum class SnowState : uint8_t { Init = 0, Main = 1, Freed = 2 };
enum class SnowSub0 : uint8_t { // [0x893c80]
    Buried = 0,   // exe 0x4a2ce0
    BurstOut = 1, // exe 0x4a2d10
    Howl = 2,     // exe 0x4a2f40
    RunOff = 3,   // exe 0x4a3080
    Lunge = 4,    // exe 0x4a3210
    Bounced = 5,  // exe 0x4a34a0
    Falling = 6,  // exe 0x4a3570
    Brake = 7,    // exe 0x4a35b0
    Stagger = 8,  // exe 0x4a36f0
    Dying = 9,    // exe 0x4a3710
    BouncedAway = 10, // exe 0x4a3860
    Retreat = 11, // exe 0x4a3940
    GiveWay = 12, // exe 0x4a3b00
};
enum class SnowSub1 : uint8_t { LeapIn = 0, Dying = 1 };               // [0x893cb4]: 0x4a3f00 / 0x4a3710
enum class SnowSub4 : uint8_t { // [0x893cbc]
    Stand = 0,  // exe 0x4a4430
    Howl = 1,   // exe 0x4a4480
    PaceOut = 2, // exe 0x4a44c0
    TurnBack = 3, // exe 0x4a45b0
    Lunge = 4,  // exe 0x4a4660
    PaceBack = 5, // exe 0x4a46b0
    TurnHome = 6, // exe 0x4a4780
    Dying = 7,  // exe 0x4a3710
};

const DataTable<CollisionBoxRecord> snowBox{TableId::SnowReaverbotBox};
const DataTable<ScalarTable<uint16_t, 4>> wakeRanges{TableId::SnowReaverbotWakeRanges}; // by the spawn kind (s8)
constexpr uint8_t kSnowChunkStageType = 0x20;
constexpr int16_t kTurnFast = 0x70, kTurnSlow = 0x40;
constexpr int16_t kRunSpeed = -0x200, kLungeSpeed = -0x3c0, kBrakeSpeed = -0xa0, kBounceSpeed = 0x100;
constexpr int kChaseDistance = 0xa00;
constexpr uint16_t kSoundHurt = 0x8d, kSoundBurst = 0x174, kSoundLunge = 0x172, kSoundBounce = 0x246;
constexpr uint16_t kSoundLeap = 0x173, kSoundStep = 0x171;
constexpr uint32_t kHitAttackSnowBite = kHitAttackThrown | kHitAttackSolid; // its body's attack (0xa0000)

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
void toSub(AnimatedActor& a, uint8_t s) {
    a.subState = s;
    a.enterStep(0);
}
// the collision scratch's point (game/projection_state: the cell gather of game/collision leaves it too)
LaneVec4& scratchPoint() { return projectionScratch().point; }
// exe 0x5e40c0(angle, r) into the scratch point as the exe stores its two return registers: x and 0, z and EDX's high
// word
void scratchAngleVector(int angle, int r) {
    int16_t dx = 0, dz = 0;
    angleVector(angle, r, dx, dz);
    LaneVec4& p = scratchPoint();
    p.x = dx;
    p.y = 0;
    p.z = dz;
    projectionScratch().pointYaw = 0;
}

// exe 0x4a4800(actor, mask): the ground / wall step: 0 on the ground (snapped), 1 the ground fell away (airborne), 2
// blocked (a wall: back to the previous position when the push-out fails) or airborne
int groundStep(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f, uint32_t mask) {
    if (a.airborne == 0) {
        const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        const int y = a.pos.y.whole;
        const int g = probeGroundAt(env, p, a.collisionBox, y - int(a.prevPos.y.whole), mask); // 0x40f1c0
        if (uint32_t(g - y + 0x3c) <= 0x78u) {
            a.pos.y = int32_t(uint32_t(g) << 16);
            return 0;
        }
        if (y <= g) {
            a.airborne = 1;
            f.bits |= kSnowAirborne;
            return 1;
        }
        if (int32_t(actorPushOutMask(env, a, mask)) < 0) a.pos = a.prevPos; // exe 0x410980
        return 2;
    }
    const uint32_t r = actorPushOutMask(env, a, mask);
    if (int32_t(r) < 0) {
        a.pos = a.prevPos;
        return 2;
    }
    if (r & 0x1000) {
        a.airborne = 0;
        f.bits &= uint8_t(~kSnowAirborne);
    }
    return 2;
}
// exe 0x4a48f0(actor, mode): the body's hit entry at (x, y - 0xa0, z): mode 0 hurts the player (the contact damage),
// else a harmless entry immune to every group; sub-type 1 a bigger one
void hitEntry(AnimatedActor& a, SnowReaverbotFields& f, uint32_t mode) {
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.result = 0;
    f.hitBox.previousZ = a.prevPos.z.whole;
    uint32_t kind, dmg, imm;
    if (mode == 0) {
        kind = kHitAttackSnowBite;
        dmg = statsOf(a).attack;
        imm = 0;
    } else {
        kind = kHitAttackBump;
        dmg = 0;
        imm = kHitImmunityMask;
    }
    LaneVec4& p = scratchPoint();
    p.x = a.pos.x.whole;
    p.y = int16_t(uint16_t(a.pos.y.whole) - 0xa0u);
    p.z = a.pos.z.whole;
    const int16_t pos[3] = {p.x, p.y, p.z};
    const uint32_t shape = a.subtype == 1 ? 0x400058u : 0x400040u;
    hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | shape, imm | dmg | kind);
}
// exe 0x4a49e0: the box's damage (the player's attack kinds): the flash, the sound, the hit points; -1 dead, 1 hurt (a
// hit of 0x20 or more, or the 8th, starts a stagger and its cool-down), 0 none
int damage(AnimatedActor& a, SnowReaverbotFields& f) {
    const uint32_t box = f.hitBox.result, dmg = box & kHitDamageMask;
    const uint8_t b = uint8_t(f.bits & ~kSnowStagger);
    f.bits = b;
    if (!(box & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) || dmg == 0) return 0;
    f.bits = uint8_t(b | kSnowHit);
    a.tint = kTintFlash;
    soundPlayAtPos(kSoundHurt, a.pos);
    if (hitPointsAdd(a, -int(dmg)) != 0) return -1;
    const int8_t c = f.hits;
    if (c >= 0) {
        const int8_t n = int8_t(c + 1);
        f.hits = n;
        if (dmg >= 0x20 || n >= 8) {
            f.hits = int8_t(0xf0);
            f.bits |= kSnowStagger;
        }
    }
    return 1;
}
// the hit spark's snow puff variant (global effect type 0, variant 3)
void puffInit(EffectActor& e) {
    e.flags = kActorLive;
    e.loopFlags |= uint8_t(kActorSweepGroup1 | kActorLateLoop);
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.variant = 3;
}
// exe 0x4a4a90(actor): a snow puff at a random point 0x100..0x11f around the actor
void puff(const AnimatedActor& a) {
    const uint32_t r = rngStep();
    scratchAngleVector(int(r & kAngleMask), int(((r >> 12) & 0x1f) + 0x100));
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    puffInit(e);
    const LaneVec4& p = scratchPoint();
    e.pos.x = int16_t(uint16_t(a.pos.x.whole) + uint16_t(p.x));
    e.pos.y = int16_t(uint16_t(a.pos.y.whole) - ((rngLastValue() >> 17) & 0xff));
    e.pos.z = int16_t(uint16_t(a.pos.z.whole) + uint16_t(p.z));
    classState<HitSparkFields>(e).size = uint16_t(((rngLastValue() >> 25) & 0x1f) + 0x180);
}
// exe 0x4a4b50(actor, p): a snow puff at the scratch point (its two dwords copied)
void puffAtScratch() {
    const uint32_t address = allocActorA0(0);
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    puffInit(e);
    e.setPositionWords(scratchPoint());
    classState<HitSparkFields>(e).size = uint16_t((rngStep() & 0x1f) + 0x100);
}
// exe 0x4a4c50(actor, yaw, big): a snow chunk (stage static type 0x20) thrown up around the actor
void chunk(const AnimatedActor& a, uint32_t yaw, bool big) {
    uint32_t r = rngStep();
    scratchAngleVector(int(r & kAngleMask), int(((r >> 12) & 0x1f) + 0x100));
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& s = staticActorAt(address);
    const LaneVec4& p = scratchPoint();
    s.flags = kActorLive;
    s.loopFlags |= kActorSweepGroup2;
    s.type = kSnowChunkStageType;
    r = rngLastValue();
    s.variant = uint8_t((r >> 17) & 1);
    s.pos.x = wholeToFixed16(int32_t(a.pos.x.whole) + p.x);
    s.pos.z = wholeToFixed16(int32_t(a.pos.z.whole) + p.z);
    s.velocity.y = int16_t(uint16_t(0xfffff800u - ((r >> 18) & 0x3ff)));
    r = rngStep();
    s.velocity.z = int16_t(uint16_t(0xffffff00u - (r & 0x1ff)));
    const uint16_t sc = uint16_t(big ? ((r >> 9) & 0x7ff) + 0x1000 : ((r >> 9) & 0x1ff) + 0x400);
    s.scale.z = int16_t(sc);
    s.scale.y = int16_t(sc);
    s.scale.x = int16_t(sc);
    r = rngStep();
    s.pos.y = wholeToFixed16(int32_t(a.pos.y.whole) - (s.scale.x >> 6) - int32_t(r & 0x3f));
    s.acceleration.y = 0xc0;
    s.rotation.y = int16_t(yaw & kAngleMask);
}
// exe 0x4a4dd0(actor): a snow chunk kicked up around the heading + a puff at it
void kick(const AnimatedActor& a) {
    uint32_t r = rngStep();
    uint32_t ang = r & 0x3ff;
    ang = uint16_t(uint16_t(ang) + uint16_t(a.rotation.y)); // a 16-bit add, the upper bits 0
    ang -= 0x200;
    scratchAngleVector(int(ang), 0x80);
    r = rngLastValue();
    LaneVec4& p = scratchPoint();
    p.x = int16_t(uint16_t(p.x) + ((r >> 10) & 0x3f) - 0x1fu);
    p.z = int16_t(uint16_t(p.z) + ((r >> 16) & 0x3f) - 0x1fu);
    const uint32_t address = allocActor60(kActorStageClass);
    if (address) {
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.type = kSnowChunkStageType;
        s.loopFlags |= kActorSweepGroup2;
        s.variant = uint8_t(ang & 1);
        s.pos.x = wholeToFixed16(int32_t(a.pos.x.whole) + p.x);
        s.pos.z = wholeToFixed16(int32_t(a.pos.z.whole) + p.z);
        r = rngStep();
        s.velocity.y = int16_t(uint16_t(0xfffff800u - (r & 0x3ff)));
        s.velocity.z = int16_t(uint16_t(0xffffff00u - ((r >> 10) & 0x1ff)));
        const uint16_t sc = uint16_t(((r >> 19) & 0x7ff) + 0x400);
        s.scale.z = int16_t(sc);
        s.scale.y = int16_t(sc);
        s.scale.x = int16_t(sc);
        r = rngStep();
        s.acceleration.y = 0xc0;
        s.pos.y = wholeToFixed16(int32_t(a.pos.y.whole) - (s.scale.x >> 6) + int32_t(r & 0x1f) - 0x80);
        s.rotation.y = int16_t((ang + 0xfffff800u) & kAngleMask);
    }
    const uint32_t effect = allocActorA0(0);
    if (!effect) return;
    if (!address) notPorted(kSnowPort, "exe 0x4a4dd0: the static pool full (the exe reads address 0x12)");
    const StaticActor& s = staticActorAt(address);
    EffectActor& e = effectActorAt(effect);
    puffInit(e);
    e.pos.x = s.pos.x.whole;
    e.pos.y = s.pos.y.whole;
    e.pos.z = s.pos.z.whole;
    classState<HitSparkFields>(e).size = uint16_t((rngStep() & 0x1f) + 0x100);
}
// exe 0x4a4bf0(actor): back to the chase: within 0xa00 of the player the lunge, else the howl's run (animation 0)
void chase(AnimatedActor& a) {
    if (uint32_t(planarDistance(a.pos, player().pos)) <= uint32_t(kChaseDistance)) {
        a.spin.y = kTurnFast;
        a.velocity.z = kLungeSpeed;
        a.anim = 0;
        a.subState = uint8_t(SnowSub0::Lunge);
    } else {
        a.velocity.z = kRunSpeed;
        a.anim = 0;
        a.subState = uint8_t(SnowSub0::Howl);
    }
    a.enterStep(0);
}
void toFall(AnimatedActor& a) { // the ground step's 1: fall
    a.velocity.y = 0;
    a.acceleration.y = 0x60;
    a.anim = 2;
    toSub(a, uint8_t(SnowSub0::Falling));
}
void toBumped(AnimatedActor& a) { // a neighbour in the way: give way
    a.spin.y = kTurnFast;
    a.velocity.z = 0;
    toSub(a, uint8_t(SnowSub0::GiveWay));
}
void toHit(AnimatedActor& a) { // hit this tick: the lunge
    a.spin.y = kTurnFast;
    a.velocity.z = 0;
    toSub(a, uint8_t(SnowSub0::Lunge));
}
void toBrake(AnimatedActor& a) {
    a.velocity.z = kBrakeSpeed;
    a.anim = 3;
    toSub(a, uint8_t(SnowSub0::Brake));
}
void bumpShake(const AnimatedActor& a) { cameraShakeAt(0x6000, 0x500, 0, 0x600, a.pos); } // exe 0x40eab0
void toBounce(AnimatedActor& a, SnowSub0 next) {
    a.velocity.z = kBounceSpeed;
    a.anim = 5;
    toSub(a, uint8_t(next));
    bumpShake(a);
}
// the ground step's tails: 2 -> a bounce (with `facing`: Bounced when the player is in front, else BouncedAway), 1 ->
// a fall
void groundTail(AnimatedActor& a, int r, bool facing) {
    if (r == 2) {
        if (!facing) {
            toBounce(a, SnowSub0::Bounced);
            return;
        }
        a.velocity.z = kBounceSpeed;
        a.anim = 5;
        a.enterStep(0);
        a.subState = uint8_t(facingAndRange(a, player(), 0x180, kChaseDistance) == kFacingOff ? SnowSub0::Bounced
                                                                                            : SnowSub0::BouncedAway);
        bumpShake(a);
        return;
    }
    if (r == 1) toFall(a);
}
// exe 0x5e3df0 by the turn rate, then the exe's unsigned window test
bool turned(AnimatedActor& a, uint32_t angle) {
    const int step = a.spin.y;
    const int d = turnYawTo(a.rotation.y, angle, step);
    return uint32_t(d + step) < uint32_t(step * 2);
}
void move(AnimatedActor& a) { integrateVelocity(a, 0, 0, a.velocity.z); } // exe 0x5e3690

// ---- sub-type 0 ---------------------------------------------------------------------------------------------------
void buried(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a2ce0: until the player is within its wake range
    if (uint32_t(planarDistance(a.pos, player().pos)) <= wakeRanges[size_t(int32_t(f.spawnKind))])
        a.subState = uint8_t(SnowSub0::BurstOut);
}
void burstOut(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a2d10: out of the snow
    if (a.step <= 3) puff(a);
    switch (a.step) {
    case 0:
        registerEnemy(a.slot, 1);
        f.timer = 0;
        ++a.step;
        return;
    case 1:
        f.timer = int16_t(f.timer + 1);
        if (f.timer == 5) ++a.step;
        return;
    case 2: {
        const uint32_t cz = uint32_t(int32_t(int16_t((a.pos.z.whole >> 9) + kCellBias)));
        const uint32_t cx = uint32_t(int32_t(int16_t((a.pos.x.whole >> 9) + kCellBias)));
        switchGridCellVariant(cx, cz, 1); // the snow mound's segment opened
        a.pos.y = int32_t(uint32_t(actorGround(env, a)) << 16);
        const int ang = angleOf(player().pos, a.pos);
        a.rotation.y = int16_t(ang);
        f.targetYaw = int16_t(ang);
        a.flags |= kActorDrawn;
        a.anim = 9;
        ++a.step;
        f.timer = 0;
        f.bits &= uint8_t(~kSnowInactive);
        for (int k = 0; int16_t(k) < kFullTurn; k += kQuarterTurn) chunk(a, (rngStep() & 0xff) + uint32_t(k), true);
        chunk(a, rngStep() & kAngleMask, false);
        chunk(a, rngStep() & kAngleMask, false);
        soundPlayAtPos(kSoundBurst, a.pos);
        return;
    }
    case 3:
        f.timer = int16_t(f.timer + 1);
        if (f.timer != 0x1e) return;
        f.timer = 0;
        ++a.step;
        return;
    case 4:
        f.timer = int16_t(f.timer + 1);
        if (f.timer == 0xf) {
            a.anim = 7;
            ++a.step;
        }
        return;
    case 5:
        if (a.animEnded()) chase(a);
        return;
    default: return;
    }
}
void howl(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a2f40: then circle or run at it
    f.bits &= uint8_t(~kSnowRunVariant);
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        a.anim = 6;
        a.step = uint8_t(s + 1);
        break;
    case 1:
        if (a.animEnded()) {
            a.anim = 0xd;
            a.step = uint8_t(s + 1);
        }
        break;
    case 2:
        if (a.animEnded()) {
            a.anim = 7;
            a.step = uint8_t(s + 1);
        }
        break;
    case 3:
        if (a.animEnded()) {
            if (uint32_t(planarDistance(a.pos, player().pos)) <= uint32_t(kChaseDistance)) {
                a.spin.y = kTurnFast;
                a.velocity.z = 0;
                a.subState = uint8_t(SnowSub0::Lunge);
            } else {
                a.spin.y = kTurnSlow;
                a.velocity.z = kRunSpeed;
                a.anim = 2;
                a.subState = uint8_t(SnowSub0::RunOff);
            }
            a.enterStep(0);
        }
        break;
    default: break;
    }
    if (f.bits & kSnowHit) toHit(a);
    if (neighbourFind(a, f.neighbour, 1)) toBumped(a);
    if (groundStep(env, a, f, 2) != 0) toFall(a);
}
void runOff(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3080: a random heading off
    switch (a.step) {                                                            // the target yaw, 0x3c ticks
    case 0: {
        a.anim = 8;
        const uint32_t r = rngStep();
        uint32_t v = (r & 0x3ff) + 0x200;
        if (!(v & 1)) v = uint32_t(-int32_t(v));
        f.targetYaw = int16_t((uint16_t(f.targetYaw) + v) & kAngleMask);
        ++a.step;
        break;
    }
    case 1:
        if (f.bits & kSnowHit) toHit(a);
        if (turned(a, uint32_t(int32_t(f.targetYaw)))) {
            f.timer = 0;
            a.velocity.z = kRunSpeed;
            a.anim = 2;
            ++a.step;
        }
        break;
    case 2:
        move(a);
        f.timer = int16_t(f.timer + 1);
        if (f.timer == 0x3c || (f.bits & kSnowHit)) toBrake(a);
        break;
    default: break;
    }
    const int r = groundStep(env, a, f, 2);
    if (neighbourFind(a, f.neighbour, 1)) toBumped(a);
    groundTail(a, r, false);
}
void lunge(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3210: face, lunge, run past
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        a.anim = 6;
        f.bits |= kSnowRunVariant;
        a.step = uint8_t(s + 1);
        break;
    case 1:
        if (a.animEnded()) {
            const int ang = angleOf(player().pos, a.pos);
            f.targetYaw = int16_t(ang);
            if (int32_t(uint32_t(uint16_t(uint16_t(ang) - uint16_t(a.rotation.y))) + 0x40) < 0x80) {
                a.velocity.z = kLungeSpeed;
                a.anim = 0xe;
                soundPlayAtPos(kSoundLunge, a.pos);
                a.step = uint8_t(a.step + 2);
            } else {
                turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(ang))), a.spin.y);
                a.anim = 9;
                ++a.step;
            }
        }
        break;
    case 2: {
        const int ang = angleOf(player().pos, a.pos);
        f.targetYaw = int16_t(ang);
        if (turned(a, uint32_t(int32_t(int16_t(ang))))) {
            a.velocity.z = kLungeSpeed;
            a.anim = 0xe;
            soundPlayAtPos(kSoundLunge, a.pos);
            ++a.step;
        }
        break;
    }
    case 3:
        if (a.animEnded()) {
            a.anim = 1;
            a.step = uint8_t(s + 1);
        }
        break;
    case 4:
        move(a);
        if (facingAndRange(a, player(), 0x180, kChaseDistance) == kFacingOff) {
            f.timer = 0;
            ++a.step;
        }
        break;
    case 5:
        move(a);
        f.timer = int16_t(f.timer + 1);
        if (f.timer == 0x1e) toBrake(a);
        break;
    default: break;
    }
    const int r = groundStep(env, a, f, 2);
    if (neighbourFind(a, f.neighbour, 1)) toBumped(a);
    groundTail(a, r, true);
}
// step 0 of the bounces: 4 chunks unless placed awake, the sound
void bounceKicks(AnimatedActor& a, SnowReaverbotFields& f) {
    f.bits &= uint8_t(~(kSnowHit | kSnowRunVariant));
    if (f.spawnKind != kSnowPlacedAwake)
        for (int k = 0; k < 4; ++k) kick(a);
    soundPlayAtPos(kSoundBounce, a.pos);
    ++a.step;
}
// step 1 of the bounces
void bounceSettle(AnimatedActor& a, SnowReaverbotFields& f) {
    if (!a.animEnded()) return;
    f.timer = 0;
    a.velocity.z = 0;
    a.anim = 0;
    ++a.step;
}
void bounced(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a34a0: off a wall
    move(a);
    const int r = groundStep(env, a, f, 2);
    switch (a.step) {
    case 0: bounceKicks(a, f); break;
    case 1: bounceSettle(a, f); break;
    case 2:
        f.timer = int16_t(f.timer + 1);
        if (f.timer >= 0x1e) chase(a);
        break;
    default: break;
    }
    if (r == 1) toFall(a);
}
void falling(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3570: until it lands
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y)); // exe 0x4a4bb0: at most 0x2000
    if (a.velocity.y > 0x2000) a.velocity.y = 0x2000;
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    groundStep(env, a, f, 2);
    if (a.airborne != 0) return;
    f.bits &= uint8_t(~kSnowHit);
    a.velocity.y = 0;
    chase(a);
}
void brake(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a35b0: then 0x1e ticks
    switch (a.step) {
    case 0:
        f.bits &= uint8_t(~(kSnowHit | kSnowRunVariant));
        move(a);
        if (a.animEnded()) {
            a.anim = 4;
            a.velocity.z = 0;
            ++a.step;
        }
        break;
    case 1:
        if (a.animEnded()) {
            if (f.bits & kSnowRetreated) {
                f.bits &= uint8_t(~kSnowRetreated);
                chase(a);
            } else {
                f.timer = 0;
                a.anim = 0;
                ++a.step;
            }
            if (f.bits & kSnowHit) toHit(a);
        }
        break;
    case 2:
        if (f.bits & kSnowHit) {
            toHit(a);
        } else {
            f.timer = int16_t(f.timer + 1);
            if (f.timer >= 0x1e) chase(a);
        }
        break;
    default: break;
    }
    groundTail(a, groundStep(env, a, f, 2), false);
}
void staggered(AnimatedActor& a) { // exe 0x4a36f0
    if (a.animEnded()) chase(a);
}
// a global static (the explosion / the blast) at its feet - 0x100
void blastAt(const AnimatedActor& a, GlobalStaticType type, uint16_t c, uint16_t e) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& x = staticActorAt(address);
    x.flags = kActorLive;
    x.loopFlags |= kActorSweepGroup2;
    x.type = typeByte(type);
    x.variant = 0;
    x.pos.x.whole = a.pos.x.whole;
    x.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x100u);
    x.pos.z.whole = a.pos.z.whole;
    if (type == GlobalStaticType::Explosion) {
        ExplosionParams p(x);
        p.spread = c;
        p.flashes = e;
    } else {
        BlastParams p(x);
        p.size = c;
        p.attack = e;
    }
}
void dyingBehaviour(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3710: blinks; explosion, blast + drops
    if (gameState().frame & 1) a.tint = kTintFlash;
    switch (a.step) {
    case 0:
        blastAt(a, GlobalStaticType::Explosion, 0x80, 0x3f);
        ++a.step;
        return;
    case 1:
        if (a.animEnded()) {
            f.timer = 0;
            a.anim = 0xb;
            ++a.step;
            f.bits |= kSnowInactive;
        }
        return;
    case 2:
        f.timer = int16_t(f.timer + 1);
        if (f.timer != 0x1e) return;
        blastAt(a, GlobalStaticType::Blast, 0xc0, 0);
        a.enterState(uint8_t(SnowState::Freed));
        if (a.subtype != 1) spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        return;
    default: return;
    }
}
void bouncedAway(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3860: then retreat
    move(a);
    const int r = groundStep(env, a, f, 2);
    switch (a.step) {
    case 0: bounceKicks(a, f); break;
    case 1: bounceSettle(a, f); break;
    case 2:
        f.timer = int16_t(f.timer + 1);
        if (f.timer >= 0x1e) {
            a.spin.y = kTurnFast;
            a.velocity.z = kLungeSpeed;
            toSub(a, uint8_t(SnowSub0::Retreat));
        }
        break;
    default: break;
    }
    if (r == 1) toFall(a);
}
void retreat(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3940: away, run 0x14 ticks
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        a.anim = 6;
        a.step = uint8_t(s + 1);
        break;
    case 1:
        if (a.animEnded()) {
            a.anim = 9;
            const uint32_t r = rngStep();
            uint32_t v = (r & 0xff) + 0x380;
            if (!(r & 0x100)) v = uint32_t(-int32_t(v));
            f.targetYaw = int16_t((uint16_t(a.rotation.y) + v) & kAngleMask);
            ++a.step;
        }
        break;
    case 2:
        if (turned(a, uint32_t(int32_t(f.targetYaw)))) {
            f.timer = 0;
            a.anim = 1;
            a.velocity.z = kLungeSpeed;
            ++a.step;
        }
        break;
    case 3:
        move(a);
        f.timer = int16_t(f.timer + 1);
        if (f.timer == 0x14) {
            f.bits |= kSnowRetreated;
            toBrake(a);
        }
        break;
    default: break;
    }
    groundTail(a, groundStep(env, a, f, 2), true);
}
void giveWay(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a3b00 (exe 0x5e5f70 / 0x5e5d90)
    const Actor& o = neighbourActor(uint16_t(f.neighbour));
    const uint16_t n = f.neighbour;
    switch (a.step) {
    case 0:
        f.timer = 0x78;
        neighbourClassify(a, f.neighbour);
        if (f.bits & kSnowRunVariant) {
            a.anim = 9;
            a.spin.y = kTurnFast;
        } else {
            a.anim = 8;
            a.spin.y = kTurnSlow;
        }
        if (n & kNeighbourOpposite) f.targetYaw = int16_t(headingFromOther(a, o, neighbourSide(f.neighbour), 0x200));
        else f.targetYaw = a.rotation.y;
        ++a.step;
        break;
    case 1:
        if (turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), a.spin.y) == 0) {
            if (f.bits & kSnowRunVariant) {
                a.velocity.z = kLungeSpeed;
                a.anim = 1;
            } else {
                a.velocity.z = kRunSpeed;
                a.anim = 2;
            }
            ++a.step;
        }
        break;
    case 2: move(a); break;
    default: break;
    }
    const bool apart = o.flags == 0 || uint32_t(int32_t(o.pos.x.whole) - a.pos.x.whole + 0x400) > 0x800u ||
                       uint32_t(int32_t(o.pos.z.whole) - a.pos.z.whole + 0x400) > 0x800u;
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer == 0 || apart) {
        if (n & kNeighbourCrossingSlow) toBrake(a);
        else chase(a);
    }
    groundTail(a, groundStep(env, a, f, 2), true);
}

// ---- sub-type 1 (a pack member) -------------------------------------------------------------------------------------
// exe 0x4a3f00 (jump table 0x4a420c): it leaps in at (0x34d5, -0x333, 0x2d5), runs to z 0x900, turns, runs on to a ledge
// and drops off it (freed after 0x10 ticks, fading)
void leapIn(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) {
    LaneVec4& p = scratchPoint();
    switch (a.step) {
    case 0:
        a.flags |= kActorDrawn;
        f.bits &= uint8_t(~kSnowInactive);
        a.anim = 0x14;
        a.prevAnim = 0xff;
        soundPlayAtPos(kSoundLeap, a.pos);
        ++a.step;
        [[fallthrough]];
    case 1: // exe 0x4a3f5c: a puff behind it every tick of the run
        if (const uint32_t address = allocActorA0(0)) {
            EffectActor& e = effectActorAt(address);
            puffInit(e);
            const uint32_t r = rngStep();
            e.pos.x = int16_t((r & 0xff) + uint16_t(a.pos.x.whole) - 0x80u);
            e.pos.y = int16_t(uint16_t(a.pos.y.whole) - 0x20u);
            e.pos.z = int16_t(uint16_t(a.pos.z.whole) - 0x80u);
            classState<HitSparkFields>(e).size = uint16_t(((rngLastValue() >> 8) & 0x1f) + 0x80);
        }
        a.velocity.x = int16_t(uint16_t(a.velocity.x) + uint16_t(a.acceleration.x));
        integrateVelocity(a, a.velocity.x, 0, 0);
        groundStep(env, a, f, 0x10);
        if (a.pos.z >= 0x9000000) {
            a.pos.z = 0x9000000;
            ++a.step;
        }
        return;
    case 2:
        a.rotation.z = int16_t(uint16_t(a.rotation.z) - 0x80u);
        if (a.rotation.z > 0) return;
        a.rotation.setZSpare(0, 0);
        a.anim = 0x15;
        ++a.step;
        return;
    case 3: {
        const uint32_t r = rngStep();
        scratchAngleVector(int(r & kAngleMask), int(((r >> 12) & 0x1f) + 0x100));
        p.x = int16_t(uint16_t(a.pos.x.whole) + uint16_t(p.x));
        p.y = int16_t(uint16_t(a.pos.y.whole) - 0x20u);
        p.z = int16_t(uint16_t(a.pos.z.whole) + uint16_t(p.z));
        puffAtScratch();
        if (a.animEnded()) {
            a.anim = 1;
            ++a.step;
        }
        return;
    }
    case 4:
        if (!(gameState().frame & 1)) {
            p.x = int16_t(uint16_t(a.pos.x.whole) + uint16_t(p.x) - 0x100u);
            p.y = int16_t(uint16_t(a.pos.y.whole) - 0x20u);
            p.z = int16_t(((rngLastValue() >> 17) & 0xff) + uint16_t(a.pos.z.whole) + uint16_t(p.z) - 0x80u);
            puffAtScratch();
        }
        move(a);
        groundStep(env, a, f, 0x10);
        if (a.airborne & 1) {
            f.timer = 0;
            f.bits |= kSnowInactive;
            a.velocity.y = 0;
            a.acceleration.y = 0xc0;
            ++a.step;
        }
        return;
    case 5: {
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        f.timer = int16_t(f.timer + 1);
        if (f.timer >= 0x10) {
            a.airborne = 0;
            a.enterState(uint8_t(SnowState::Freed));
            a.flags &= uint8_t(~kActorDrawn);
        }
        const int16_t t = f.timer;
        if (t >= 8) {
            const uint32_t c = uint32_t(0x20 - int32_t(t) * 2);
            a.tint = uint16_t(((c << 5 | c) << 5) | c);
        }
        return;
    }
    default: return;
    }
}

// the tail of 0x4a2bd0 / 0x4a3e10 / 0x4a4330: the hit entry, the animation, the shadow's ground probe, the footsteps
void tail(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) {
    if (!(f.bits & kSnowInactive)) hitEntry(a, f, f.hitEntryMode);
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80 / 0x5e1c30(anim, 0)
    else animSetPose(a, a.anim, 0);
    // exe 0x5f20f0(a, 0xc0, 0x100, 0): the shadow's entry is render output, but its ground probe (exe 0x40f190, when
    // not on a platform and the list 0x6d0070 has room) leaves the collision scratch the next pack member's snow puffs
    // copy (exe 0x4a4b50): the probe is kept for that side effect
    if (a.flags & kActorDrawn) shadowGroundProbe(env, a);
    if ((a.anim == 1 || a.anim == 2) && (a.keyIndex == 3 || a.keyIndex == 9)) soundPlayAtPos(kSoundStep, a.pos);
}
// the hit points below 0: the dying behaviour, animation 0xa
void died(AnimatedActor& a, SnowReaverbotFields& f, uint8_t behaviour) {
    a.subState = behaviour;
    a.enterStep(0);
    a.anim = 0xa;
    a.velocity.z = 0;
    f.timer = 0;
    ++f.hitEntryMode;
}
// the common head of the inits (sub-types 1 / 4): the hit points (unchecked row), the motion cleared
void initMotion(AnimatedActor& a) {
    const uint16_t hp = statsOf(a).hitPoints;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.acceleration.setZSpare(0, 0);
    a.acceleration.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
}

void subType0(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) {
    switch (SnowState(a.state)) {
    case SnowState::Init: { // exe 0x4a2ad0
        if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
            freeActor(a);
            return;
        }
        a.anim = 0;
        a.prevAnim = 0xff;
        const uint16_t hp = hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0;
        a.maxHitPoints = int16_t(hp);
        a.hitPoints = int16_t(hp);
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.velocity.setZSpare(0, 0);
        a.velocity.setXY(0, 0);
        a.spin.x = 0;
        a.spin.y = kTurnFast;
        a.collisionBox.pointAt(snowBox.stateAddress());
        a.actorHit = 0;
        a.platform = nullptr;
        a.clearClassBlock();
        animSet(a, a.anim, 0); // exe 0x5e1c30
        animPose(a);
        a.enterState(uint8_t(SnowState::Main));
        if (f.spawnKind == kSnowPlacedAwake) {
            a.pos.y = int32_t(uint32_t(actorGround(env, a)) << 16);
            registerEnemy(a.slot, 1);
            a.anim = 0;
            a.flags |= kActorDrawn;
            a.subState = uint8_t(SnowSub0::Howl);
            return;
        }
        f.bits = kSnowInactive;
        a.flags &= uint8_t(~kActorDrawn);
        return;
    }
    case SnowState::Main: // exe 0x4a2bd0
        if (!(f.bits & kSnowInactive)) {
            if (damage(a, f) == -1) {
                died(a, f, uint8_t(SnowSub0::Dying));
            } else if ((f.bits & (kSnowStagger | kSnowAirborne)) == kSnowStagger) {
                a.subState = uint8_t(SnowSub0::Stagger);
                a.velocity.z = 0;
                a.anim = 0xc;
            }
        }
        switch (SnowSub0(a.subState)) {
        case SnowSub0::Buried: buried(a, f); break;
        case SnowSub0::BurstOut: burstOut(env, a, f); break;
        case SnowSub0::Howl: howl(env, a, f); break;
        case SnowSub0::RunOff: runOff(env, a, f); break;
        case SnowSub0::Lunge: lunge(env, a, f); break;
        case SnowSub0::Bounced: bounced(env, a, f); break;
        case SnowSub0::Falling: falling(env, a, f); break;
        case SnowSub0::Brake: brake(env, a, f); break;
        case SnowSub0::Stagger: staggered(a); break;
        case SnowSub0::Dying: dyingBehaviour(a, f); break;
        case SnowSub0::BouncedAway: bouncedAway(env, a, f); break;
        case SnowSub0::Retreat: retreat(env, a, f); break;
        case SnowSub0::GiveWay: giveWay(env, a, f); break;
        default: notPortedAt(kSnowPort, "0x4a2a50 sub 0 behaviour", a.subState);
        }
        tail(env, a, f);
        return;
    case SnowState::Freed: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt(kSnowPort, "0x4a2a50 sub 0 state", a.state);
    }
}
void subType1(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) {
    switch (SnowState(a.state)) {
    case SnowState::Init: { // exe 0x4a3d20
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.anim = 1;
        a.scale.z = 0x300;
        a.scale.y = 0x300;
        a.scale.x = 0x300;
        a.flags &= uint8_t(~kActorDrawn);
        a.prevAnim = 0xff;
        initMotion(a);
        a.velocity.x = kLungeSpeed;
        a.velocity.y = 0;
        a.velocity.z = -0x640;
        a.spin.x = 0;
        a.spin.y = kTurnFast;
        a.collisionBox = nullptr;
        a.actorHit = 0;
        a.platform = nullptr;
        a.clearClassBlock();
        f.bits = kSnowInactive;
        a.pos.x = wholeToFixed16(0x34d5);
        a.pos.y = wholeToFixed16(-0x333);
        a.pos.z = wholeToFixed16(0x2d5);
        a.rotation.y = kQuarterTurn;
        a.rotation.z = 0x200;
        a.enterState(uint8_t(SnowState::Main));
        return;
    }
    case SnowState::Main: // exe 0x4a3e10
        if (!(f.bits & kSnowInactive) && damage(a, f) == -1) died(a, f, uint8_t(SnowSub1::Dying));
        switch (SnowSub1(a.subState)) {
        case SnowSub1::LeapIn: leapIn(env, a, f); break;
        case SnowSub1::Dying: dyingBehaviour(a, f); break; // [0x893cb4 + 4] = 0x4a3710
        default:
            notPortedAt(kSnowPort, "0x4a2a50 sub 1 behaviour (0x4a4430..: the sub-4 table, not reached from 0 / 1)",
                        a.subState);
        }
        tail(env, a, f);
        return;
    case SnowState::Freed: freeActor(a); return;
    default: notPortedAt(kSnowPort, "0x4a2a50 sub 1 state", a.state);
    }
}

// ---- sub-type 4 (the sentry): it paces from its spawn point along the spawn heading by its distance << 4, turns, paces
// back (ST4F layout C). The pacing's ticks: 1 - (distance << 4) / the speed (negative; cdq / idiv)
uint16_t paceTicks(int32_t dist, const AnimatedActor& a) { return uint16_t(1u - uint32_t((dist << 4) / int32_t(a.velocity.z))); }
void sentryStand(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a4430: 0x20 ticks
    if (a.step == 0) {
        f.timer = 0;
        a.anim = 0;
        ++a.step;
    } else if (a.step == 1) {
        f.timer = int16_t(f.timer + 1);
        if (f.timer > 0x1f) toSub(a, uint8_t(SnowSub4::Howl));
    }
}
void sentryHowl(AnimatedActor& a) { // exe 0x4a4480: howl (animation 6), then pace out (animation 1)
    if (a.step == 0) {
        a.anim = 6;
        a.step = 1;
    } else if (a.step == 1 && a.animEnded()) {
        a.anim = 1;
        toSub(a, uint8_t(SnowSub4::PaceOut));
    }
}
void sentryPaceOut(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a44c0: out, then onto the far point
    if (a.step == 0) {
        f.timer = int16_t(paceTicks(int32_t(f.paceDistance), a));
        ++a.step;
    } else if (a.step == 1) {
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer != 0) {
            move(a);
            return;
        }
        // exe 0x5e4070(out, the spawn yaw, distance << 4): (sin, 0, cos)(yaw + a half turn) * r, into the scratch
        // 0x6d0160 (its 4th dword, an uninitialised stack word in the exe, is not written here)
        int32_t v[3];
        angleRadiusVector(int(uint16_t(f.homeYaw)), int(uint32_t(f.paceDistance) << 4), v);
        std::array<Lane32, 3>& out = projectionScratch().transformed;
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
        a.pos.x = int32_t((uint32_t(int32_t(f.home.x)) << 16) + uint32_t(out[0]));
        a.subState = uint8_t(SnowSub4::TurnBack);
        a.pos.z = int32_t((uint32_t(int32_t(f.home.z)) << 16) + uint32_t(out[2]));
        a.enterStep(0);
    }
}
void sentryTurnBack(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a45b0: toward the spawn point
    if (a.step == 0) {
        const int ang = angleOf(wholeToFixed16(f.home.x), wholeToFixed16(f.home.z), a.pos.x, a.pos.z);
        f.targetYaw = int16_t(ang);
        a.anim = 9;
        ++a.step;
    } else if (a.step == 1) {
        if (turned(a, uint32_t(int32_t(f.targetYaw)))) {
            a.anim = 1;
            toSub(a, uint8_t(SnowSub4::PaceBack));
        }
    }
}
void sentryLunge(AnimatedActor& a) { // exe 0x4a4660: the lunge's animation, then pace back
    if (a.step == 0) {
        a.anim = 0xe;
        soundPlayAtPos(kSoundLunge, a.pos);
        ++a.step;
    } else if (a.step == 1 && a.animEnded()) {
        a.anim = 1;
        toSub(a, uint8_t(SnowSub4::PaceBack));
    }
}
void sentryPaceBack(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a46b0: the distance back, then onto it
    if (a.step == 0) {
        LaneVec4& p = scratchPoint();
        p.x = a.pos.x.whole;
        p.y = a.pos.y.whole;
        p.z = a.pos.z.whole;
        // exe 0x5e3b40(scratch, spawn): the x / z distance of the s16 points
        const int dx = wrap16(f.home.x - p.x), dz = wrap16(f.home.z - p.z);
        f.timer = int16_t(paceTicks(isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz))), a));
        ++a.step;
    } else if (a.step == 1) {
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer != 0) {
            move(a);
            return;
        }
        a.pos.x = wholeToFixed16(f.home.x);
        a.pos.y = wholeToFixed16(f.home.y);
        a.pos.z = wholeToFixed16(f.home.z);
        toSub(a, uint8_t(SnowSub4::TurnHome));
    }
}
void sentryTurnHome(AnimatedActor& a, SnowReaverbotFields& f) { // exe 0x4a4780: to the spawn yaw, animation 7, stand
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 9;
        a.step = 1;
    } else if (s == 1) {
        if (turned(a, uint32_t(int32_t(f.homeYaw)))) {
            a.anim = 7;
            ++a.step;
        }
    } else if (s == 2 && a.animEnded()) {
        toSub(a, uint8_t(SnowSub4::Stand));
    }
}
void subType4(const PlayerEnv& env, AnimatedActor& a, SnowReaverbotFields& f) {
    switch (SnowState(a.state)) {
    case SnowState::Init: { // exe 0x4a4230: the model variant 3 for the spawn byte 6, else 0
        if (!bindActorModel(a, a.spawnByte == 6 ? 3 : 0)) {
            freeActor(a);
            return;
        }
        a.prevAnim = 0;
        a.flags |= kActorDrawn;
        a.anim = 0;
        initMotion(a);
        a.velocity.setXY(0, 0);
        a.velocity.z = kLungeSpeed;
        a.spin.x = 0;
        a.spin.y = kTurnFast;
        a.collisionBox.pointAt(snowBox.stateAddress());
        a.actorHit = 0;
        a.platform = nullptr;
        a.clearClassBlock();
        f.home.x = a.pos.x.whole;
        f.home.y = a.pos.y.whole;
        f.home.z = a.pos.z.whole;
        f.homeYaw = a.rotation.y;
        animSet(a, a.anim, 0); // exe 0x5e1c30
        animPose(a);
        a.enterState(uint8_t(SnowState::Main));
        return;
    }
    case SnowState::Main: // exe 0x4a4330
        if (!(f.bits & kSnowInactive) && damage(a, f) == -1) died(a, f, uint8_t(SnowSub4::Dying));
        switch (SnowSub4(a.subState)) {
        case SnowSub4::Stand: sentryStand(a, f); break;
        case SnowSub4::Howl: sentryHowl(a); break;
        case SnowSub4::PaceOut: sentryPaceOut(a, f); break;
        case SnowSub4::TurnBack: sentryTurnBack(a, f); break;
        case SnowSub4::Lunge: sentryLunge(a); break;
        case SnowSub4::PaceBack: sentryPaceBack(a, f); break;
        case SnowSub4::TurnHome: sentryTurnHome(a, f); break;
        case SnowSub4::Dying: dyingBehaviour(a, f); break; // [0x893cbc + 7 * 4] = 0x4a3710
        default: notPortedAt(kSnowPort, "0x4a2a50 sub 4 behaviour", a.subState);
        }
        a.pos.y = int32_t(uint32_t(actorGround(env, a)) << 16); // exe 0x40f190
        tail(env, a, f);
        return;
    case SnowState::Freed: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt(kSnowPort, "0x4a2a50 sub 4 state", a.state);
    }
}

void snowReaverbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4a2a50
    SnowReaverbotFields& f = classState<SnowReaverbotFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (a.subtype) {
    case 0: subType0(env, a, f); break;
    case 1: subType1(env, a, f); break;
    case 4: subType4(env, a, f); break; // [0x893c6c + 4 * 4] = 0x893c60 (sub-types 2 / 3: null)
    default: notPortedAt(kSnowPort, "0x4a2a50 sub", a.subtype);
    }
    if (f.hits < 0) f.hits = int8_t(uint8_t(f.hits) + 1u);
}

} // namespace

void snowReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { snowReaverbot(env, animatedActorAt(actor)); }

} // namespace od::port
