// The guardian reaverbot class 0x455480 (see guardian_reaverbot.hpp). States [0x88ed54]: 0 init 0x4555c0, 1 0x4556b0
// (-> 2), 2 main 0x4556c0 (sub-states [0x88ed68]), 3 death 0x4568f0, 4 0x52ef00 (nothing). Its animations are
// model-bank indices keyed by the behaviour; the shadow (exe 0x5f20f0) is render output.
#include "game/actor_state.hpp"
#include "game/guardian_reaverbot_state.hpp"
#include "game/guardian_reaverbot.hpp"

#include <array>
#include <initializer_list>

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
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/line_of_sight.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shockwave.hpp"
#include "game/spark_emitter.hpp"
#include "game/table_records.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace guardian_reaverbot_state;

namespace {

constexpr const char* kGuardianPort = "guardian reaverbot port"; // the area of its port-gap errors

// +0x15f
constexpr uint8_t kGuardianBlocked = 0x01;    // the wall test hit a wall this tick
constexpr uint8_t kGuardianAttacking = 0x02;
constexpr uint8_t kGuardianAttackDone = 0x04;
constexpr uint8_t kGuardianStrongFist = 0x08; // the fists carry the strong damage (cleared by the hit boxes)
constexpr uint8_t kGuardianHoldPose = 0x10;   // the pose held (a fist stuck)
constexpr uint8_t kGuardianSideAttack = 0x20; // the attack starts at its aim (from a wall)
constexpr uint8_t kGuardianNoDamage = 0x40;
constexpr uint8_t kGuardianTurning = 0x80;
constexpr uint8_t kGuardianWallHit = 0x01;    // +0x167: the push-out's result had a wall (& 0xf00)
// the line-of-sight probe's byte
constexpr uint8_t kSightRunning = 0x80, kSightDone = 0x40, kSightBlocked = 0x01;
// sight()'s answer
constexpr uint32_t kSightOutsideCone = 1, kSightFar = 2, kSightProbeBlocked = 3;
constexpr uint32_t kSightNear = 0x80, kSightProbing = 0x40;

enum class GuardianState : uint8_t {
    Init = 0,  // exe 0x4555c0
    Wake = 1,  // exe 0x4556b0
    Main = 2,  // exe 0x4556c0
    Death = 3, // exe 0x4568f0
    Done = 4,  // exe 0x52ef00
};
enum class GuardianSub : uint8_t {
    Stand = 0,   // exe 0x455760
    Walk = 1,    // exe 0x4557f0
    Turn = 2,    // exe 0x455950 (its steps: GuardianTurn)
    LookAround = 3, // exe 0x455d00
    Blocked = 4, // exe 0x455de0
    Attack = 5,  // exe 0x455f60 (its steps: GuardianAttack)
    Reel = 6,    // exe 0x4565d0
};
enum class GuardianTurn : uint8_t { Free = 0, Recoil = 1, Flinch = 2 }; // exe 0x455970 / 0x455b90 / 0x455c70
enum class GuardianAttack : uint8_t {
    WindUp = 0,  // exe 0x455f80
    Aim = 1,     // exe 0x456060
    Slam = 2,    // exe 0x4560e0
    RightFist = 3, // exe 0x456290
    LeftFist = 4,  // exe 0x456390
    Stuck = 5,   // exe 0x4564d0
    End = 6,     // exe 0x456510
};
enum class GuardianHead : uint8_t { Straight = 0, Shake = 1, TowardPlayer = 2 }; // [0x88ede8]

const DataTable<ScalarTable<uint8_t, 8>> guardianModels{TableId::GuardianReaverbotModels}; // the model variant by the spawn byte
const DataTable<CollisionBoxRecord> guardianCollisionBox{TableId::GuardianReaverbotCollisionBox};
const DataTable<RecordTable<ShortPoint4, 4, 8>> glowOffsets{TableId::GuardianReaverbotGlowOffsets}; // the glow trails' offsets on bone 1
const DataTable<ScalarTable<int8_t, 0x20>> turnChoices{TableId::GuardianReaverbotTurnChoices}; // [free mask * 4 + RNG & 3]: -1 / 0 / 1 steps
const DataTable<ScalarTable<uint8_t, 8>> walkLoopsFirst{TableId::GuardianReaverbotWalkLoopsFirst}, walkLoopsSecond{TableId::GuardianReaverbotWalkLoopsSecond};
const DataTable<ScalarTable<uint8_t, 0x10>> attackCooldowns{TableId::GuardianReaverbotAttackCooldowns};
const DataTable<ScalarTable<uint8_t, 4>> sparkBones{TableId::GuardianReaverbotSparkBones};
constexpr uint32_t kFistWallBoxSize = 0xc; // exe data: the fist probes' collision boxes
constexpr uint32_t kGuardianTint = 0x20a0ff; // the HUD gauge's bar colour
constexpr uint32_t kGlowBone = 1;
constexpr uint8_t kArmFlashA = 0x40, kArmFlashB = 0x44;
constexpr uint32_t kArmBones = 2, kArmBonesLast = 9;
constexpr int kTurnProbeDistance = 0x177, kTurnProbeHeight = 100;
constexpr int kSightDistance = 5000, kNearDistance = 600;
constexpr uint8_t kPatience = 0x1e, kPatienceInView = 0x3c;
constexpr int16_t kLowHitPoints = 0x40; // above: its patience runs; at or below 0x40 (0x41 for the sparks) it hurries
constexpr int16_t kInvulnerableTicks = 0x14;
constexpr int16_t kWalkSpeed = -0x180, kStandDrift = -0x80, kReelBack = -0x60;
constexpr uint16_t kSoundHit = 0x8d, kSoundWeakPoint = 0x8e, kSoundSlam = 0xb6, kSoundRightFist = 0xb5;
constexpr uint16_t kSoundLeftFist = 0xb7, kSoundCollapse = 0x162, kSoundFootstep = 0xb4;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
// exe 0x5e1810: the bone's point (a bone past the actor's blocks is the port gap)
ShortVec4& bone(const AnimatedActor& a, uint32_t b) { return bonePoint(a, b, kGuardianPort); }
// the angle of the guardian seen from the player (exe 0x5e3bc0(player, +0x10)) and its yaw's difference to it
int angleToPlayer(const AnimatedActor& a) { return angleOf(player().pos, a.pos); }
int facing(const AnimatedActor& a) { return angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleToPlayer(a))); }

// exe 0x4571e0: the velocity and the acceleration cleared (dwords)
void zeroVelocity(AnimatedActor& a) {
    a.velocity.clearWords();
    a.acceleration.clearWords();
}
// exe 0x456020(actor, b): the arm bones' scale words {b, 0x40, b, 0} (exe 0x5e1850): the arms' flash
void armColour(AnimatedActor& a, uint8_t b) {
    const uint32_t c = uint32_t(b) | 0x4000u | uint32_t(b) << 16;
    for (uint32_t k = kArmBones; k <= kArmBonesLast; ++k) setBoneScaleWord(a, k, c);
}
// the glow trail of slot s
EffectActor& glowOf(uint8_t s) { return effectPool()[size_t(s) - kEffectClass]; }
// exe 0x4573d0(actor, v): each live glow trail's draw mode = v
void glowMode(GuardianFields& f, uint8_t v) {
    for (size_t k = 0; k < 4; ++k) {
        const uint8_t s = f.glowSlots[k];
        if (s == 0) continue;
        EffectActor& e = glowOf(s);
        if (e.flags & kActorLive) classState<GlowTrailFields>(e).mode = v;
    }
}
// exe 0x457420: the glow trails freed, the slots cleared
void glowFree(GuardianFields& f) {
    for (size_t k = 0; k < 4; ++k) {
        const uint8_t s = f.glowSlots[k];
        if (s == 0) continue;
        EffectActor& e = glowOf(s);
        if (e.flags & kActorLive) freeActor(e);
    }
    f.clearGlowSlots();
}
// exe 0x457340: four glow trails (stage effect type 6, variant 1, width 6) on bone 1 at the offsets 0x88ee20; a full
// pool writes 0 to slot +0x168 + k (k counting down from 3) while the successes fill the slots from +0x168 up (as the
// exe)
void glowSpawn(AnimatedActor& a, GuardianFields& f) {
    size_t slot = 0, offset = 0;
    for (int k = 3; k >= 0; --k) {
        const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (!address) {
            f.glowSlots[size_t(k)] = 0;
            continue;
        }
        EffectActor& e = effectActorAt(address);
        GlowTrailFields& t = classState<GlowTrailFields>(e);
        e.flags = kActorLive;
        e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
        e.type = kGlowTrailStageType;
        e.variant = uint8_t(GlowTrailVariant::Trail);
        t.size = 6;
        t.mode = 0;
        t.parent = &a;
        const ShortPoint4 o = glowOffsets[offset];
        e.pos.x = o.x;
        e.pos.y = o.y;
        e.pos.z = o.z;
        t.bone = kGlowBone;
        f.glowSlots[slot] = e.slot;
        ++slot;
        ++offset;
    }
}
// exe 0x456fc0: the wall test, the push-out (its result kept; < 0: back to the previous position; a wall -> the wall
// bit), the height of the ground; the wall test's answer
int collide(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    f.wallBits = 0;
    const int32_t r = int32_t(actorTestMove(env, a));
    if (r < 0) {
        const uint32_t u = actorPushOut(env, a);
        a.wallHit = u;
        if (int32_t(u) < 0) a.pos = a.prevPos;
        if (u & 0xf00) f.wallBits |= kGuardianWallHit;
    }
    a.pos.y.whole = int16_t(actorGround(env, a));
    return r;
}
// exe 0x455a60(actor, step, cone): the next yaw of a turn -- the directions yaw - step / yaw / yaw + step probed 0x177
// ahead from 100 above the feet; with a cone the player's side (1 left of it, 4 right, 2 inside) first, else a free
// direction from the table [mask][RNG & 3]; all blocked: turn round. The transcription tests the probes' unsigned
// answers below 0 -- never true: every direction counts as free
int pickTurn(const PlayerEnv& env, const AnimatedActor& a, int step, int cone) {
    const uint32_t mask = 7;
    const int yaw = uint16_t(a.rotation.y) & kAngleMask;
    const int16_t p[3] = {a.pos.x.whole, wrap16(a.pos.y.whole - kTurnProbeHeight), a.pos.z.whole};
    uint32_t ang = uint32_t(yaw - step);
    for (int k = 2; k >= 0; --k) {
        probeMoveAhead(env, a, p, int(ang), kTurnProbeDistance);
        ang += uint32_t(step);
    }
    uint32_t dir = 0;
    if (cone != 0) {
        const int d = facing(a);
        dir = 2;
        if (d < -cone) dir = 4;
        if (cone < d) dir = 1;
    }
    if (dir & mask) return (1 - int((dir & mask) >> 1)) * step + yaw;
    const uint32_t r = rngStep();
    return turnChoices[mask * 4 + (r & 3)] * step + yaw;
}
// exe 0x456720(actor, cone): kSightOutsideCone, kSightFar (5000 or more away), else 0 | kSightNear (within 600) and a
// line-of-sight probe toward it (kSightProbing while it runs); kSightProbeBlocked once the probe came back blocked
uint32_t sight(AnimatedActor& a, GuardianFields& f, int cone) {
    uint32_t lo = kSightOutsideCone, hi = 0;
    const int d = facing(a);
    if (uint32_t(d + cone) > uint32_t(cone * 2)) return lo;
    lo = kSightFar;
    const int dist = planarDistance(a.pos, player().pos);
    if (dist >= kSightDistance) return lo;
    lo = 0;
    if (dist < kNearDistance) hi = kSightNear;
    const uint8_t b = f.sight;
    if (!(b & kSightRunning)) {
        lineOfSightSpawn(a, kAnimatedLayers, bone(a, 1), 0x1000020, 0, f.sight, 8);
    } else if (b & kSightDone) {
        if (b & kSightBlocked) lo = kSightProbeBlocked;
        f.sight = 0;
        return lo | hi;
    }
    return lo | hi | kSightProbing;
}
// exe 0x4567e0: out of sight for its patience -> look around
void patienceTo3(AnimatedActor& a, GuardianFields& f, int cone) {
    if ((sight(a, f, cone) & 0xf) == 0) {
        const uint8_t c = uint8_t(f.patience - 1);
        f.patience = c;
        if (c == 0) {
            f.patience = 0;
            a.subState = uint8_t(GuardianSub::LookAround);
            a.enterStep(0);
        }
    } else {
        f.patience = kPatience;
    }
}
// exe 0x456830: the same, 1 when the patience runs out
int patience(AnimatedActor& a, GuardianFields& f, int cone) {
    if ((sight(a, f, cone) & 0xf) == 0) {
        const uint8_t c = uint8_t(f.patience - 1);
        f.patience = c;
        if (c == 0) {
            f.patience = 0;
            return 1;
        }
    } else {
        f.patience = kPatience;
    }
    return 0;
}
// exe 0x456870(actor, cone): the player near and no cooldown -> the attack (1); above 0x40 hit points: in plain view
// the patience is 0x3c, else it runs down -> the turn's recoil (-1)
int attackCheck(AnimatedActor& a, GuardianFields& f, int cone) {
    const uint32_t r = sight(a, f, cone);
    if ((r & kSightNear) && f.cooldown == 0) {
        a.subState = uint8_t(GuardianSub::Attack);
        a.enterStep(0);
        f.bits &= uint8_t(~kGuardianSideAttack);
        return 1;
    }
    if (a.hitPoints > kLowHitPoints) {
        if ((r & 0x4f) == 0) {
            f.patience = kPatienceInView;
        } else {
            const uint8_t c = uint8_t(f.patience - 1);
            f.patience = c;
            if (c == 0) {
                a.subState = uint8_t(GuardianSub::Turn);
                a.enterStep(uint8_t(GuardianTurn::Recoil));
                return -1;
            }
        }
    }
    return 0;
}
// exe 0x457200(actor, cone): the cone when the player is outside it, else 0
uint32_t outsideCone(const AnimatedActor& a, uint32_t cone) {
    const int d = facing(a);
    return cone * 2 < uint32_t(d) + cone ? cone : 0;
}
// exe 0x456f60(actor, pos16, radius): a strong hit entry (compared heights) with the strong damage
void strongHit(AnimatedActor& a, GuardianFields& f, const int16_t p[3], uint32_t radius) {
    hitListSubmit(p, f.hitBox, uint32_t(int32_t(radius | 0x1a00000) >> 2) | hitOwner(a.slot),
                  uint32_t(statsOf(a).strongAttack) | kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown);
}
// exe 0x457300(variant, x, y, z): a ground shock
void shockSpawn(uint8_t v, int16_t x, int16_t y, int16_t z) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    s.variant = v;
    s.pos.x.whole = x;
    s.loopFlags |= kActorSweepGroup2;
    s.flags = kActorLive;
    s.type = kShockwaveStageType;
    ShockwaveParams(s).downward = 0;
    s.pos.y.whole = y;
    s.pos.z.whole = z;
}
// exe 0x457030(actor, pos16, attack, box): the fist's probe box at the point; a breakable wall there queued (attack |
// 0x30000); -1 = a wall without strength, 1 = the wall stronger than the attack, else 0
int wallProbe(const PlayerEnv& env, const AnimatedActor& a, const int16_t p[3], int attack, uint32_t box) {
    const uint32_t mask = collisionLayerMask(a.slot);
    const uint32_t r = testMoveBlocked(env, p, p, CollisionBoxRef::record(stateAddress(TableId::GuardianReaverbotFistWallBoxes) + box * kFistWallBoxSize), mask);
    if (int32_t(r) < 0) {
        queueBreakableWallHit(r, attack | int(breakLevel(kBreakLevelMax)));
        const uint32_t w = cellSegmentStrength(r);
        if (int32_t(w) < 1) return -1;
        if (attack < int(int16_t(w))) return 1;
    }
    return 0;
}
// exe 0x456560(actor, pos16, box): the fist in a wall stronger than 0x80 while the animation's event bit 8 is up: the
// pose held (stuck), a shake at the point; 1
int fistStuck(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f, const int16_t p[3], uint32_t box) {
    if (wallProbe(env, a, p, 0x80, box) > 0 && (a.animKey.events & 8)) {
        f.bits |= kGuardianHoldPose;
        cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, p);
        return 1;
    }
    return 0;
}
// exe 0x456510: the attack's animation over -> done, a cooldown from the table, the walk
void attackEnd(AnimatedActor& a, GuardianFields& f) {
    if (!a.animEnded()) return;
    f.bits |= kGuardianAttackDone;
    const uint32_t r = rngStep();
    f.cooldown = attackCooldowns[r & 0xf];
    a.subState = uint8_t(GuardianSub::Walk);
    a.enterStep(0);
}

// exe 0x4570e0: the damage -- the body box (0xc0000 flags: a hit; its damage counts 1, the sound, and unless attacking
// / turning the flinch, the arms flash) and the weak point (hit within a quarter turn of the facing: its damage, the
// sound, the reel); the flash, dead -> state 3 (-1); a 0x400000 hit: 0x14 ticks of invulnerability
int damage(AnimatedActor& a, GuardianFields& f) {
    HitBox& weak = guardianWeakPointBox();
    const uint32_t b2 = weak.result, b1 = f.hitBox.result;
    uint32_t v = 0;
    if (f.invulnerable != 0) return 0;
    if (b1 & 0xc0000) {
        if (b1 & kHitDamageMask) {
            soundPlayAtPos(kSoundHit, a.pos);
            v = 1;
        }
        if (!(f.bits & (kGuardianTurning | kGuardianAttacking))) {
            a.subState = uint8_t(GuardianSub::Turn);
            a.enterStep(uint8_t(GuardianTurn::Flinch));
            armColour(a, kArmFlashA);
        }
    }
    if (b2 & 0xc0000) {
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(weak.contact.x)));
        if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
            v += b2 & kHitDamageMask;
            soundPlayAtPos(kSoundWeakPoint, a.pos);
            a.subState = uint8_t(GuardianSub::Reel);
            a.enterStep(0);
        }
    }
    if (v != 0) {
        a.tint = kTintFlash;
        if (hitPointsAdd(a, wrap16(-int(v))) != 0) {
            a.enterState(uint8_t(GuardianState::Death));
            return -1;
        }
        if ((b2 | b1) & kHitAttackStrongPush) f.invulnerable = kInvulnerableTicks;
    }
    return 0;
}

// ---- sub-states -----------------------------------------------------------------------------------------------------
void stand(AnimatedActor& a, GuardianFields& f) { // exe 0x455760: 3 animation loops or until blocked -> the turn
    if (a.step == 0) {
        a.enterStep(1);
        a.anim = 2;
        zeroVelocity(a);
        a.velocity.z = kStandDrift;
        a.spin.setXY(0, 0);
        f.counter = 3;
        f.bits &= 0x7c;
    }
    if (a.animEnded()) f.counter = int16_t(f.counter - 1);
    if (f.counter == 0 || (f.bits & kGuardianBlocked)) {
        zeroVelocity(a);
        a.subState = uint8_t(GuardianSub::Turn);
        a.enterStep(0);
    }
    patienceTo3(a, f, 0x200);
}
void walk(AnimatedActor& a, GuardianFields& f) { // exe 0x4557f0: walk at the player, loops from the two tables
    switch (a.step) {
    case 0: {
        a.enterStep(1);
        a.anim = 3;
        zeroVelocity(a);
        a.velocity.z = kWalkSpeed;
        uint8_t b = f.bits;
        if (b & kGuardianAttackDone) {
            f.patience = kPatienceInView;
            b &= uint8_t(~kGuardianAttackDone);
        }
        f.setHeadMode(uint8_t(GuardianHead::TowardPlayer));
        f.bits = uint8_t((b & ~kGuardianBlocked) | kGuardianAttacking);
        glowMode(f, 0);
    }
        [[fallthrough]];
    case 1:
        a.enterStep(2);
        f.counter = walkLoopsFirst[rngStep() & 7];
        [[fallthrough]];
    case 2:
        turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x20);
        if (a.animEnded()) {
            f.counter = int16_t(f.counter - 1);
            if (f.counter == 0) a.enterStep(2); // (the exe writes 2: steps 3 / 4 are not reached from here)
        }
        break;
    case 3:
        a.enterStep(3);
        f.counter = walkLoopsSecond[rngStep() & 7];
        [[fallthrough]];
    case 4:
        if (a.animEnded()) {
            f.counter = int16_t(f.counter - 1);
            if (f.counter == 0) a.enterStep(1);
        }
        break;
    default: break;
    }
    attackCheck(a, f, 0x300);
    if (f.bits & kGuardianBlocked) {
        a.subState = uint8_t(GuardianSub::Blocked);
        a.enterStep(0);
    }
}
void turnFree(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x455970: to a free direction
    switch (a.stepCounter) {
    case 0:
        a.stepCounter = 1;
        f.target = int16_t(pickTurn(env, a, (f.bits & kGuardianBlocked) ? 0x400 : 0x200, 0));
        a.spin.y = 0x18;
        a.anim = 1;
        zeroVelocity(a);
        f.bits &= 0x7d;
        [[fallthrough]];
    case 1:
        a.stepCounter = 2;
        f.setHeadMode(uint8_t(GuardianHead::Shake));
        [[fallthrough]];
    case 2:
        if (f.headMode == uint8_t(GuardianHead::Straight)) {
            a.stepCounter = 3;
            patienceTo3(a, f, 0x300);
            return;
        }
        break;
    case 3:
        a.stepCounter = 4;
        a.anim = 2;
        [[fallthrough]];
    case 4: {
        const int st = a.spin.y;
        const int d = turnToward(a.rotation.y, int(f.target), st);
        if (uint32_t(d + st) <= uint32_t(st * 2)) {
            a.subState = uint8_t(GuardianSub::Stand);
            a.enterStep(0);
        }
        break;
    }
    default: break;
    }
    patienceTo3(a, f, 0x300);
}
void turnRecoil(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x455b90: a recoil, then walk / look
    const uint8_t c = a.stepCounter;
    bool toStep2 = false;
    if (c == 0) {
        if (a.animEnded()) {
            a.stepCounter = 1;
            f.target = int16_t(pickTurn(env, a, 0x200, 0));
            a.spin.y = 0x10;
            a.anim = 0;
            zeroVelocity(a);
            f.patience = kPatience;
            f.bits |= kGuardianAttacking;
            toStep2 = true;
        }
    } else if (c == 1) {
        toStep2 = true;
    } else if (c == 2) {
        if (a.animEnded()) {
            if (a.hitPoints > kLowHitPoints) {
                a.subState = uint8_t(GuardianSub::LookAround);
                a.enterStep(1);
            } else {
                a.subState = uint8_t(GuardianSub::Walk);
                a.enterStep(0);
            }
        }
    }
    if (toStep2) {
        a.stepCounter = 2;
        a.anim = 0x14;
    }
    if (patience(a, f, 0x300) != 0) {
        a.subState = uint8_t(GuardianSub::Walk);
        a.enterStep(0);
    }
    glowMode(f, (gameState().frame & 4) ? 0 : 3);
}
void turnFlinch(AnimatedActor& a, GuardianFields& f) { // exe 0x455c70: while the player stays in the cone
    const uint8_t c = a.stepCounter;
    if (c == 0) {
        a.stepCounter = 1;
        a.anim = 1;
        f.setHeadMode(uint8_t(GuardianHead::Shake));
        zeroVelocity(a);
        f.bits |= kGuardianTurning;
    }
    if (c == 0 || c == 1) {
        if (outsideCone(a, 0x300) == 0 || f.headMode == uint8_t(GuardianHead::Straight)) {
            a.subState = uint8_t(GuardianSub::LookAround);
            a.enterStep(0);
        }
    }
    glowMode(f, (gameState().frame & 4) ? 0 : 3);
}
void turn(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x455950: [0x88ed94]
    switch (GuardianTurn(a.step)) {
    case GuardianTurn::Free: turnFree(env, a, f); break;
    case GuardianTurn::Recoil: turnRecoil(env, a, f); break;
    case GuardianTurn::Flinch: turnFlinch(a, f); break;
    default: notPortedAt(kGuardianPort, "guardian sub 2 step", a.step);
    }
}
void lookAround(AnimatedActor& a, GuardianFields& f) { // exe 0x455d00: 0x455d20 (the glows) / 0x455d80 (glows off)
    if (a.step == 0) { // exe 0x455d20
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 0xb;
            f.setHeadMode(uint8_t(GuardianHead::TowardPlayer));
            zeroVelocity(a);
            if (f.glowSlotsWord() == 0) glowSpawn(a, f);
            f.bits |= uint8_t(kGuardianAttackDone | kGuardianAttacking);
        } else if (a.stepCounter == 1 && a.animEnded()) {
            a.subState = uint8_t(GuardianSub::Walk);
            a.enterStep(0);
        }
        return;
    }
    if (a.stepCounter == 0) { // exe 0x455d80
        a.stepCounter = 1;
        a.anim = 0xc;
        zeroVelocity(a);
        f.bits |= kGuardianAttacking;
        return;
    }
    if (a.animEnded()) {
        if (f.glowSlotsWord() != 0) glowFree(f);
        a.subState = uint8_t(GuardianSub::Stand);
        a.enterStep(0);
    }
}
void blocked(AnimatedActor& a, GuardianFields& f) { // exe 0x455de0: a wall -- turn from it (+-0x20 a tick) or attack
    if (a.step != 0) {
        a.rotation.y = int16_t(a.rotation.y + a.spin.y);
        if (!(f.wallBits & kGuardianWallHit)) {
            a.subState = uint8_t(GuardianSub::Walk);
            a.enterStep(0);
        }
        attackCheck(a, f, 0x300);
        return;
    }
    f.bits |= kGuardianAttacking;
    const uint32_t s = sight(a, f, 0x300) & 0xf;
    bool attack = s == 0;
    if (!attack) {
        uint32_t bits = 0xfd77;
        // exe 0x5e3a60(+0x10, the player): the 3D distance of the integer parts (unsigned compare)
        const uint32_t dist = uint32_t(distance3(a.pos, player().pos));
        if (dist > 1000 || s < kSightFar) bits = 0x288;
        const uint32_t r = rngStep();
        attack = (bits & (1u << (r & 0xf))) != 0;
    }
    if (attack && f.cooldown == 0) {
        a.subState = uint8_t(GuardianSub::Attack);
        a.enterStep(0);
        f.bits |= kGuardianSideAttack;
        attackCheck(a, f, 0x300);
        return;
    }
    uint32_t r = a.actorHit;
    a.enterStep(1);
    a.anim = 3;
    if (r == 0) r = a.wallHit;
    int32_t x = 0, z = 0;
    if (r & 0x100) x = int32_t(0xff000000u);
    if (r & 0x200) x = 0x1000000;
    if (r & 0x400) z = int32_t(0xff000000u);
    if (r & 0x800) z = 0x1000000;
    // exe 0x5e3bc0(local, +0x10), local = (+0x10 + x, _, +0x18 + z): the position seen from the local point
    const int ang = atanAngle(float(int32_t(0u - uint32_t(x))), float(int32_t(0u - uint32_t(z)))) & kAngleMask;
    const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang));
    a.spin.y = d >= 0 ? 0x20 : -0x20;
    f.setHeadMode(uint8_t(GuardianHead::TowardPlayer));
    f.patience = uint8_t(f.patience + 0xf);
    attackCheck(a, f, 0x300);
}

// sub 5 (exe 0x455f60: [0x88edbc]): the attacks
void attack(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f);
void aim(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f);
void windUp(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x455f80: the arms flash 0x1e ticks
    if (a.stepCounter == 0) {
        if (f.bits & kGuardianSideAttack) {
            a.enterStep(uint8_t(GuardianAttack::Aim));
            aim(env, a, f); // exe 0x456060
            f.bits &= uint8_t(~kGuardianSideAttack);
            return;
        }
        a.stepCounter = 1;
        a.anim = 0;
        armColour(a, kArmFlashA);
        zeroVelocity(a);
        f.counter = 0x1e;
    }
    armColour(a, uint8_t((uint8_t(f.counter) & 1) * 4 + kArmFlashA));
    f.counter = int16_t(f.counter - 1);
    if (f.counter == 0) {
        glowMode(f, 0);
        armColour(a, kArmFlashA);
        a.enterStep(uint8_t(GuardianAttack::Aim));
    }
}
void aim(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x456060: by the player's side
    const int d = facing(a);
    if (uint32_t(d + 0x100) < 0x200) a.enterStep(uint8_t(GuardianAttack::Slam));
    else if (d < 1) a.enterStep(uint8_t(GuardianAttack::LeftFist));
    else a.enterStep(uint8_t(GuardianAttack::RightFist));
    f.bits |= kGuardianAttacking;
    zeroVelocity(a);
    attack(env, a, f);
}
// the point between the hands 100 ahead: Ry(yaw) (0, 0, -100) + the mean of bones 5 / 9 in x / z, bone 5 in y
void slam(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x4560e0: the two fists (anim 6)
    int flags = int8_t(a.animKey.events);
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        a.anim = 6;
        flags = 0;
    }
    const int16_t ahead[3] = {0, 0, -100};
    int32_t v[3];
    transformVector(rotationY(int(uint32_t(int32_t(a.rotation.y)) & kAngleMask)), ahead, v); // exe 0x40bea0 / 0x40c800
    const ShortVec4& p5 = bone(a, 5), p9 = bone(a, 9);
    const int16_t pt[3] = {wrap16(((int(p9.x) + int(p5.x)) >> 1) + v[0]), wrap16(int(p5.y) + v[1]),
                           wrap16(((int(p9.z) + int(p5.z)) >> 1) + v[2])}; // the exe's local point
    if (flags & 1) strongHit(a, f, pt, 0x100);
    if (flags & 2) {
        shockSpawn(kShockwaveSequencer, pt[0], a.pos.y.whole, pt[2]);
        soundPlayAtPos(kSoundSlam, a.pos);
        cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, pt);
    }
    if ((flags & 4) && fistStuck(env, a, f, pt, 0)) {
        a.enterStep(uint8_t(GuardianAttack::Stuck));
        a.anim = 0xe;
    }
    attackEnd(a, f);
}
void rightFist(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x456290 (anim 7)
    const uint8_t flags = a.animKey.events;
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        a.anim = 7;
    }
    const ShortVec4& p5 = bone(a, 5);
    const int16_t pt[3] = {p5.x, wrap16(p5.y + 0x96), p5.z};
    if (flags & 1) strongHit(a, f, pt, 0xb4);
    if (flags & 2) {
        shockSpawn(kShockwaveSequencer | 1, p5.x, wrap16(a.pos.y.whole + 0x10), p5.z);
        soundPlayAtPos(kSoundRightFist, a.pos);
        cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, pt);
    }
    if ((flags & 4) && fistStuck(env, a, f, pt, 1)) {
        a.enterStep(uint8_t(GuardianAttack::Stuck));
        a.anim = 0xf;
        f.animKeyStart = uint8_t(0x41 - uint8_t(a.animKey.frame));
    }
    attackEnd(a, f);
}
void leftFist(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x456390 (anim 0x11)
    const uint8_t flags = a.animKey.events;
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        f.target = 0;
        a.anim = 0x11;
    }
    const ShortVec4& b5 = bone(a, 5), b9 = bone(a, 9);
    const int16_t p5[3] = {b5.x, b5.y, b5.z}, p9[3] = {b9.x, b9.y, b9.z};
    if (flags & 1) {
        strongHit(a, f, p9, 0xb4);
        f.bits |= kGuardianStrongFist;
        if (f.target == 0) {
            soundPlayAtPos(kSoundLeftFist, a.pos);
            f.target = 1;
        }
    }
    if (flags & 0x10) strongHit(a, f, p5, 0xb4);
    if (flags & 2) {
        shockSpawn(kShockwaveSequencer | 1, p9[0], wrap16(a.pos.y.whole + 0x10), p9[2]);
        cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, p9);
    }
    int stuck = 0;
    if (flags & 4) stuck = fistStuck(env, a, f, p9, 1);
    if (flags & 0x20) stuck |= fistStuck(env, a, f, p5, 1);
    if (stuck) {
        a.enterStep(uint8_t(GuardianAttack::Stuck));
        a.anim = 0x13;
        f.animKeyStart = uint8_t(0x24 - uint8_t(a.animKey.frame));
    }
    attackEnd(a, f);
}
void stuck(AnimatedActor& a, GuardianFields& f) { // exe 0x4564d0: stuck in the wall 4 ticks (the pose held)
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        f.counter = 4;
    }
    f.counter = int16_t(f.counter - 1);
    f.bits |= kGuardianHoldPose;
    if (f.counter == 0) a.enterStep(uint8_t(GuardianAttack::End));
}
void attack(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    switch (GuardianAttack(a.step)) {
    case GuardianAttack::WindUp: windUp(env, a, f); break;
    case GuardianAttack::Aim: aim(env, a, f); break;
    case GuardianAttack::Slam: slam(env, a, f); break;
    case GuardianAttack::RightFist: rightFist(env, a, f); break;
    case GuardianAttack::LeftFist: leftFist(env, a, f); break;
    case GuardianAttack::Stuck: stuck(a, f); break;
    case GuardianAttack::End: attackEnd(a, f); break; // exe 0x456510
    default: notPortedAt(kGuardianPort, "guardian attack step", a.step);
    }
}
void reel(AnimatedActor& a, GuardianFields& f) { // exe 0x4565d0: a weak-point hit (anim 9 / 10), back off, turn
    switch (a.step) {
    case 0: {
        a.enterStep(1);
        const uint8_t b = uint8_t(f.bits | kGuardianNoDamage);
        f.bits = b;
        a.anim = (b & kGuardianAttacking) ? 10 : 9;
        zeroVelocity(a);
        f.setHeadMode(uint8_t(GuardianHead::Straight));
        armColour(a, kArmFlashA);
        if (f.glowSlotsWord() == 0) glowSpawn(a, f);
        glowMode(f, 0);
        return;
    }
    case 1:
        a.velocity.z = a.animKey.events ? kReelBack : 0;
        if (a.animEnded()) {
            a.enterStep(2);
            f.bits &= uint8_t(~kGuardianNoDamage);
        }
        return;
    case 2:
        a.enterStep(3);
        a.anim = 3;
        f.setHeadMode(uint8_t(GuardianHead::TowardPlayer));
        f.bits |= uint8_t(kGuardianAttackDone | kGuardianAttacking);
        f.target = int16_t(angleToPlayer(a));
        a.spin.y = 0x40;
        [[fallthrough]];
    case 3: {
        const int st = a.spin.y;
        const int d = turnYawTo(a.rotation.y, uint32_t(int32_t(f.target)), st);
        if (uint32_t(d + st) <= uint32_t(st * 2) || outsideCone(a, 0x300) == 0) {
            a.subState = uint8_t(GuardianSub::Walk);
            a.enterStep(0);
        }
        return;
    }
    default: return;
    }
}

void mainState(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x4556c0 (state 2)
    a.tint = kTintNeutral;
    if (f.invulnerable != 0) f.invulnerable = int16_t(f.invulnerable - 1);
    if (!(f.bits & kGuardianNoDamage) && damage(a, f) != 0) return;
    switch (GuardianSub(a.subState)) {
    case GuardianSub::Stand: stand(a, f); break;
    case GuardianSub::Walk: walk(a, f); break;
    case GuardianSub::Turn: turn(env, a, f); break;
    case GuardianSub::LookAround: lookAround(a, f); break;
    case GuardianSub::Blocked: blocked(a, f); break;
    case GuardianSub::Attack: attack(env, a, f); break; // exe 0x455f60
    case GuardianSub::Reel: reel(a, f); break;
    default: notPortedAt(kGuardianPort, "guardian sub-state", a.subState);
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    if (f.cooldown != 0) --f.cooldown;
    if (collide(env, a, f) != 0 && (f.wallBits & kGuardianWallHit)) f.bits |= kGuardianBlocked;
}

void init(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) { // exe 0x4555c0 (state 0)
    if (!bindActorModel(a, guardianModels[a.spawnByte])) {
        freeActor(a);
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp =
        hasAnimatedStats(a.type) ? uint16_t(uint32_t(statsOf(a).hitPoints) << 2) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(guardianCollisionBox.stateAddress());
    a.clearInteractionBytes();
    a.airborne = 0;
    a.onPlatform = 0;
    f.clearInvulnerableBytes();
    a.wallHit = 0;
    f.clearBlockWords();
    const ShortVec4& p = bone(a, 0);
    f.hitBox.previousX = p.x;
    f.hitBox.previousZ = p.z;
    HitBox& weak = guardianWeakPointBox();
    weak.previousX = p.x;
    weak.previousZ = p.z;
    f.patience = kPatience;
    resetBoneScales(a);
    hudAddBossGauge(a, 1, 1, kGuardianTint); // exe 0x415d30
    collide(env, a, f);
    a.state = uint8_t(GuardianState::Wake);
    a.subState = 0;
    a.step = 0;
    a.stepCounter = 0;
}

// exe 0x4568f0 (state 3): the collapse (anim 0xd), 14 blasts at random bones, the last blast, the drops
void death(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    a.tint = kTintNeutral;
    switch (a.subState) {
    case 0:
        a.subState = 1;
        [[fallthrough]];
    case 1:
        a.subState = 2;
        a.anim = 0xd;
        f.setHeadMode(uint8_t(GuardianHead::Straight));
        zeroVelocity(a);
        integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
        collide(env, a, f);
        glowMode(f, 0);
        countDefeat(); // exe 0x5e53e0
        return;
    case 2:
        if (a.animKey.events != 0) {
            soundPlayAtPos(kSoundCollapse, a.pos);
            cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, a.pos);
        }
        if (a.animEnded()) a.subState = 3;
        return;
    case 3:
        a.subState = 4;
        f.target = 0xe;
        f.counter = 0;
        [[fallthrough]];
    case 4: {
        const int16_t c = f.counter;
        f.counter = int16_t(c + 1);
        if (c % 3 != 0) return;
        const ShortVec4& p = bone(a, rngStep() & 0xf);
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300: the blast (game/explosions)
            StaticActor& x = staticActorAt(address);
            x.flags = kActorLive;
            x.type = typeByte(GlobalStaticType::Blast);
            x.variant = 0;
            x.loopFlags |= kActorSweepGroup2;
            x.pos.x.whole = p.x;
            x.pos.y.whole = p.y;
            x.pos.z.whole = p.z;
            const uint32_t r = rngStep();
            BlastParams blast(x);
            blast.attack = 1;
            blast.size = uint16_t(((r & 3) + 8) << 4);
            a.tint = 0x421f;
        }
        f.target = int16_t(f.target - 1);
        if (f.target == 0) a.subState = 5;
        return;
    }
    case 5: {
        if (const uint32_t address = allocActor60(0)) {
            StaticActor& x = staticActorAt(address);
            x.flags = kActorLive;
            x.type = typeByte(GlobalStaticType::Blast);
            x.loopFlags |= kActorSweepGroup2;
            x.variant = 1;
            BlastParams blast(x);
            x.pos.x = a.pos.x;
            x.pos.y = a.pos.y;
            blast.size = 0x180;
            x.pos.z = a.pos.z;
            blast.attack = 4;
        }
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);
        return;
    }
    default: return;
    }
}

void headTick(AnimatedActor& a, GuardianFields& f) { // exe 0x456b20: [0x88ede8]
    switch (GuardianHead(f.headMode)) {
    case GuardianHead::Straight: f.headYaw = 0; return; // exe 0x456b40
    case GuardianHead::Shake: {                          // exe 0x456b50: +-0x20 three times by 8, then back to 0
        const uint8_t ph = f.shakePhase;
        switch (ph) {
        case 0: case 2: case 4: {
            const int8_t c = int8_t(uint8_t(f.headYaw) + 8);
            f.headYaw = c;
            if (c > 0x20) {
                f.headYaw = 0x20;
                f.shakePhase = uint8_t(ph + 1);
            }
            return;
        }
        case 1: case 3: case 5: {
            const int8_t c = int8_t(uint8_t(f.headYaw) - 8);
            f.headYaw = c;
            if (c < -0x20) {
                f.headYaw = -0x20;
                f.shakePhase = uint8_t(ph + 1);
            }
            return;
        }
        case 6: {
            const int8_t c = int8_t(uint8_t(f.headYaw) + 8);
            f.headYaw = c;
            if (c > 0) {
                f.headYaw = 0;
                f.setHeadMode(uint8_t(GuardianHead::Straight));
            }
            return;
        }
        default: return;
        }
    }
    case GuardianHead::TowardPlayer: { // exe 0x456c00: (the difference >> 4) within +-0x20
        const int8_t c = int8_t(uint8_t(facing(a) >> 4));
        f.headYaw = c;
        if (c > 0x20) f.headYaw = 0x20;
        if (f.headYaw < -0x20) f.headYaw = -0x20;
        return;
    }
    default: notPortedAt(kGuardianPort, "guardian head mode (null table entry)", f.headMode);
    }
}

// exe 0x456c50: the hit list entries -- bone 0 (the body), bones 12 / 15, the arm capsules 2-3-4-5 and 6-7-8-9 (the
// fists carry the strong damage while kGuardianStrongFist, cleared here), then the weak point at bone 0 + Ry(yaw) (0,
// 0x80, 100). The contact damage from the stats; dead: the bump attack instead of the solid one
void hitBoxes(AnimatedActor& a, GuardianFields& f) {
    uint32_t base = kHitAttackSolid;
    HitBox& weak = guardianWeakPointBox();
    const AnimatedActorStats t = statsOf(a);
    f.hitBox.result = 0;
    weak.result = 0;
    const uint32_t dmg1 = t.attack;
    uint32_t fistFlags, fistDmg;
    if (!(f.bits & kGuardianStrongFist)) {
        fistFlags = kHitAttackSolid;
        fistDmg = dmg1;
    } else {
        fistFlags = kHitAttackStrong | kHitAttackThrown;
        fistDmg = t.strongAttack;
    }
    if (a.hitPoints < 0) {
        base = kHitAttackBump;
        fistFlags = kHitAttackBump;
    }
    const uint32_t owner = hitOwner(a.slot);
    uint32_t p2 = base | dmg1;
    auto at3 = [](const ShortVec4& p) { return std::array<int16_t, 3>{p.x, p.y, p.z}; };
    hitListSubmit(at3(bone(a, 0)).data(), f.hitBox, owner | 0x40002d, p2);
    p2 |= kHitAttackHarmless;
    hitListSubmit(at3(bone(a, 0xc)).data(), f.hitBox, owner | 0x480014, p2);
    hitListSubmit(at3(bone(a, 0xf)).data(), f.hitBox, owner | 0x480014, p2);
    const uint32_t fist = fistDmg | fistFlags | kHitAttackHarmless;
    for (uint32_t first : {2u, 6u}) {
        auto prev = at3(bone(a, first));
        for (uint32_t k = first + 1; k < first + 3; ++k) {
            const auto q = at3(bone(a, k));
            hitListSubmitCapsule(prev.data(), 0x12, q.data(), 0x12, f.hitBox, owner | 0x481000, p2);
            prev = q;
        }
        hitListSubmitCapsule(prev.data(), 0x12, at3(bone(a, first + 3)).data(), 0x12, f.hitBox, owner | 0x481000,
                             fist);
    }
    f.bits &= uint8_t(~kGuardianStrongFist);
    const ShortVec4& b0 = bone(a, 0);
    f.hitBox.previousX = b0.x;
    f.hitBox.previousZ = b0.z;
    const int16_t up[3] = {0, 0x80, 100};
    int32_t v[3];
    transformVector(rotationY(int(uint32_t(int32_t(a.rotation.y)) & kAngleMask)), up, v); // (render state skipped)
    const int16_t pt[3] = {wrap16(v[0] + b0.x), wrap16(b0.y + v[1]), wrap16(b0.z + v[2])};
    hitListSubmit(pt, weak, owner | 0x488014, p2);
    weak.previousX = pt[0];
    weak.previousZ = pt[2];
}

// exe 0x457240: below 0x41 hit points, on 2 of 16 RNG values, sparks (exe 0x4572b0: a spark emitter, 0xf ticks) at
// bone 0 / 1 / 1 / 0 (0x88ee1c)
void lowHpSparks(AnimatedActor& a) {
    if (a.hitPoints >= kLowHitPoints + 1) return;
    if (!((1u << (rngStep() & 0xf)) & 0x180u)) return;
    const ShortVec4& p = bone(a, sparkBones[rngStep() & 3]);
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& s = staticActorAt(address);
    s.flags = kActorLive;
    s.loopFlags |= kActorSweepGroup2;
    s.type = kSparkEmitterStageType;
    s.variant = 0;
    s.pos.x.whole = p.x;
    s.pos.y.whole = p.y;
    s.pos.z.whole = p.z;
    SparkEmitterParams& sp = classState<SparkEmitterParams>(s);
    sp.driftX = 0;
    sp.driftZ = 0;
    sp.lifetime = 0xf;
}

void guardian(const PlayerEnv& env, AnimatedActor& a) { // exe 0x455480
    GuardianFields& f = classState<GuardianFields>(a);
    if (!(f.bits & kGuardianHoldPose)) a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    f.bits &= uint8_t(~kGuardianHoldPose);
    switch (GuardianState(a.state)) {
    case GuardianState::Init: init(env, a, f); break;
    case GuardianState::Wake: a.enterState(uint8_t(GuardianState::Main)); break; // exe 0x4556b0
    case GuardianState::Main: mainState(env, a, f); break;
    case GuardianState::Death: death(env, a, f); break;
    case GuardianState::Done: break; // exe 0x52ef00: nothing
    default: notPortedAt(kGuardianPort, "guardian state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    headTick(a, f);
    if (!(f.bits & kGuardianHoldPose)) {
        if (a.anim == a.prevAnim) {
            animStep(a);
        } else {
            animSet(a, a.anim, f.animKeyStart);
            f.animKeyStart = 0;
        }
        // the head's yaw into bone 2's decoded angle (the decode buffer's entry 2, its y lane)
        LaneVec4 headAngles = decodedFrame().entries[2];
        headAngles.y = int16_t(uint16_t(wrap16(int(f.headYaw) << 4)) + uint16_t(headAngles.y));
        animPose(a);
    }
    hitBoxes(a, f);
    lowHpSparks(a);
    // exe 0x5f20f0(actor, 0xc0, 0x160, 0): the shadow (render output)
    if (a.animKey.events & 0x80) {
        soundPlayAtPos(kSoundFootstep, a.pos); // the footstep
        cameraShakeAt(0xa00, 0x200, 0x100, 0x800, a.pos);
    }
}

} // namespace

HitBox& guardianWeakPointBox() {
    static HitBox s;
    return s;
}

void guardianReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { guardian(env, animatedActorAt(actor)); }

} // namespace od::port
