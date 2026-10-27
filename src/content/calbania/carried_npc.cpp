// The carried NPC, class 0x4f2ee0 (pool 0x20 type 0x71; ST30 sub 2 variant 0x81 = the one carried out of Glyde's base
// in the escape, Shu -- hypothesis), moved here from game/carry. States 0x896df0 (0 init 0x4f3060, 1 0x4f3130, 2
// carried / flying 0x4f3d40). Variant 0x8N: behaviour [0x896e08 + N * 4] (0x80: 0x4f3190, sub-states 0x896e2c: 0
// idle 0x4f3200, 1 hit 0x4f32a0; 0x81: 0x4f33a0, sub-states 0x896e34: 0 idle 0x4f3430, 1 falling 0x4f34a0, 2 hit
// 0x4f34f0, 3 wander 0x4f3620); the carried sub-states 0x896e54: 0 in the hands 0x4f3d60, 1 flying 0x4f3eb0, 2 landed
// 0x4f3f50. The variants below 0x80 are script cast members ([0x896e10 + v * 4]): each step waits for the script to
// change +0xc (+0xd = the last seen), then plays the next animation.
#include "content/calbania/carried_npc.hpp"
#include "content/calbania/carried_npc_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/rewards.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/actors.hpp"
#include "game/address_forms.hpp"
#include "game/cutscene_actor.hpp"
#include "game/vectors.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace carried_npc_state;

namespace {

using address_forms::E;

constexpr const char* kCarriedNpcPort = "carried NPC"; // the area of its port-gap errors

// +0x160
constexpr uint8_t kNpcNoHitEntry = 0x01;
constexpr uint8_t kNpcNoHitReaction = 0x02;
constexpr uint8_t kNpcAttackEntry = 0x04; // the thrown body's hit entry
constexpr uint8_t kNpcBounces = 0x08;     // walls reflect its rotation
constexpr uint8_t kNpcNoFall = 0x10;
// +0x161
constexpr uint8_t kNpcBlocked = 0x04;     // landed / bounced: the fall stops
constexpr uint8_t kNpcGroundLost = 0x08;
constexpr uint8_t kHoldFrame = 0x80;
constexpr uint8_t kRunFlagLifted = 0x10;  // +1 bit 4 while lifted (read by the draw pass)

enum class NpcState : uint8_t {
    Init = 0,    // exe 0x4f3060
    Main = 1,    // exe 0x4f3130
    Carried = 2, // exe 0x4f3d40
};
enum class WorkerSub : uint8_t { // variant 0x81
    Idle = 0,    // exe 0x4f3430
    Falling = 1, // exe 0x4f34a0
    Hit = 2,     // exe 0x4f34f0
    Wander = 3,  // exe 0x4f3620
};
enum class IdlerSub : uint8_t { // variant 0x80
    Idle = 0, // exe 0x4f3200
    Hit = 1,  // exe 0x4f32a0
};
enum class CarriedSub : uint8_t {
    InHands = 0, // exe 0x4f3d60
    Flying = 1,  // exe 0x4f3eb0
    Landed = 2,  // exe 0x4f3f50
};
constexpr uint8_t kWorkerVariant = 0x80; // the variants with a behaviour (not a script cast member)

constexpr uint32_t kHitAny = 0x75c0000, kHitStrong = 0x480000, kHitShot = 0x40000, kHitNoFlash = 0x7000000;
constexpr uint32_t kHitThrown = 0x20000;
constexpr int16_t kGravity = 0x40;
constexpr uint16_t kSoundIdle = 0x1cf, kSoundLand = 0x1c9, kSoundLifted = 0x1cb, kSoundCarried = 0x1cc;
constexpr uint16_t kSoundHitA = 0x1cd, kSoundHitB = 0x1ce, kSoundHit = 0x8d;
constexpr uint16_t kIdleSoundTicks = 0x12c;
constexpr int kWanderArrive = 0x20;
constexpr int kWanderSpeed = -0xc0;
constexpr uint8_t kPlayerAnimGive = 0x93; // the player's animation of the hand-over (variant 4 waits for its step 4)
constexpr uint8_t kPlayerAnimGiveStep = 4;

const DataTable<CollisionBoxRecord> carriedNpcBox{TableId::CarriedNpcBox};
const DataTable<InteractionBoxRecord> carriedNpcLiftBox{TableId::CarriedNpcLiftBox};
const DataTable<ScalarTable<uint8_t, 8>> castAnims{TableId::CarriedNpcCastAnims}; // the first animation of a cast variant
const DataTable<ScalarTable<uint8_t, 4>> workerAnims{TableId::CarriedNpcWorkerAnims}; // ... of a worker variant (& 0x7f)
const DataTable<ScalarTable<uint16_t, 8>> wanderPoints{TableId::CarriedNpcWanderPoints}; // {x, z} pairs, by an even index

// its stats row (by type)
AnimatedActorStats statsOf(const AnimatedActor& actor) { return animatedStats(actor.type); }
// its hit box (the class block's head)
HitBox& hitBoxOf(AnimatedActor& actor) { return classState<CarriedNpcFields>(actor).hitBox; }

// exe 0x4ac3b0: landed / blocked -> 1
int npcFall(AnimatedActor& actor, CarriedNpcFields& npc, int yaw) {
    if (!(npc.collisionBits & kNpcBlocked) && (actor.velocity.y < 1 || actor.onPlatform == 0)) {
        moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        actor.velocity.y += uint16_t(actor.acceleration.y);
        return 0;
    }
    actor.velocity.z = int16_t(actor.velocity.z >> 1);
    actor.velocity.y = int16_t(uint32_t(-int(actor.velocity.y)) >> 1);
    actor.rotation.x = 0;
    actor.airborne = 1;
    return 1;
}
// exe 0x4f3fa0: 1 = the ground lost (> 0x10)
int npcCollide(AnimatedActor& actor, CarriedNpcFields& npc) {
    uint32_t r = 0;
    npc.collisionBits = 0;
    if (int32_t(actorTestMove(*E, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(*E, actor);              // exe 0x410960
        actor.wallHit = r;
        r |= actor.actorHit;
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (npc.bits & kNpcBounces) {
            int16_t pitch = actor.rotation.x, yaw = actor.rotation.y;
            const int floor = reflectRotationOffWalls(pitch, yaw, r); // exe 0x410740 on +0x28
            actor.rotation.x = pitch;
            actor.rotation.y = yaw;
            if (floor != 0) npc.collisionBits |= kNpcBlocked;
        }
    }
    const int g = actorGround(*E, actor); // exe 0x40f190
    if (actor.airborne == 0 && actor.onPlatform == 0) {
        if (0x10 < g - actor.pos.y.whole) {
            npc.collisionBits |= kNpcGroundLost;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(npc.bits & kNpcBounces) && (r & 0x3000)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        npc.collisionBits |= kNpcBlocked;
    }
    return 0;
}
// exe 0x4f4070: the balance lost, the flash, the hit yaw
int npcHit(AnimatedActor& actor, CarriedNpcFields& npc) {
    const HitBox& hit = hitBoxOf(actor);
    const uint32_t r = hit.result;
    if ((r & 0xfff) == 0 || (r & kHitAny) == 0) return 0;
    if (r & kHitStrong) balanceAdd(-4 * int(statsOf(actor).balanceLossStrong)); // exe 0x5e51d0
    else if (r & kHitShot) balanceAdd(-4 * int(statsOf(actor).balanceLossHit));
    if (!(r & kHitNoFlash)) {
        actor.tint = npc.damageTaken < 0x3c ? kTintFlash : uint16_t(0x3def);
        npc.damageTaken += r & 0xfff;
    }
    npc.hitYaw = uint16_t(hit.contact.x);
    return 1;
}
// exe 0x4f4130: not within 0x20 of the target
bool npcAway(const AnimatedActor& actor, CarriedNpcFields& npc) {
    return !(uint16_t(npc.targetX - uint16_t(actor.pos.x.whole) + kWanderArrive) < 2 * kWanderArrive &&
             uint16_t(npc.targetZ - uint16_t(actor.pos.z.whole) + kWanderArrive) < 2 * kWanderArrive);
}
// every 301 ticks
void idleSound(const AnimatedActor& actor, CarriedNpcFields& npc, bool strictlyPast) {
    const uint16_t t = npc.soundTimer;
    npc.soundTimer = uint16_t(t + 1u);
    if (strictlyPast ? t > kIdleSoundTicks - 1 : t >= kIdleSoundTicks) {
        npc.soundTimer = 0;
        soundPlayAtPos(kSoundIdle, actor.pos);
    }
}
// exe 0x4f3430: stands, wanders off when away
void workerIdle(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (actor.step == 0) {
        npc.bits = kNpcNoFall;
        actor.step = 1;
        actor.anim = 0x26;
    } else if (actor.step == 1 && npcAway(actor, npc)) {
        actor.subState = uint8_t(WorkerSub::Wander);
        actor.enterStep(0);
    }
    idleSound(actor, npc, false);
}
// exe 0x4f34a0: falls (lifted run flag set) until it lands
void workerFalling(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (actor.step == 0) {
        npc.bits = kNpcNoHitEntry | kNpcBounces | kNpcNoFall;
        actor.step = 1;
        actor.velocity.y = 0;
        actor.acceleration.y = kGravity;
    } else if (actor.step != 1) {
        return;
    }
    if (npcFall(actor, npc, actor.rotation.y) != 0) {
        actor.airborne = 0;
        actor.velocity.y = 0;
        actor.subState = uint8_t(WorkerSub::Idle);
        actor.enterStep(0);
    }
}
// exe 0x4f34f0 (variant 0x81: animations 0x27..0x29, a random cry) / 0x4f32a0 (variant 0x80: 0x21..0x23, sound 0x8d):
// turned away from the hit, knocked up and back, lands, gets up
void hitReaction(AnimatedActor& actor, CarriedNpcFields& npc, bool worker) {
    switch (actor.step) {
    case 0:
        npc.bits = kNpcNoHitEntry | kNpcNoHitReaction | kNpcBounces | kNpcNoFall;
        actor.step = 1;
        actor.anim = worker ? 0x27 : 0x21;
        actor.rotation.y = int16_t(uint16_t(npc.hitYaw + uint16_t(kHalfTurn)));
        npc.timer = 2;
        soundPlayAtPos(worker ? ((crtRand() & 1) ? kSoundHitA : kSoundHitB) : kSoundHit, actor.pos);
        return;
    case 1:
        --npc.timer;
        if (npc.timer != 0) return;
        actor.velocity.x = 0;
        actor.velocity.z = 0x140;
        ++actor.step;
        actor.velocity.y = -0x140;
        actor.acceleration.y = kGravity;
        return;
    case 2:
        if (npcFall(actor, npc, actor.rotation.y) == 0) return;
        actor.anim = worker ? 0x28 : 0x22;
        actor.airborne = 0;
        ++actor.step;
        if (worker) soundPlayAtPos(kSoundLand, actor.pos);
        return;
    case 3:
        if (actor.animKey.flags & kKeyJump) {
            if (worker) {
                ++actor.step;
                actor.anim = 0x29;
            } else {
                actor.anim = 0x23;
                ++actor.step;
            }
        }
        return;
    case 4:
        if (actor.animKey.flags & kKeyJump) actor.enterState(uint8_t(NpcState::Main));
        return;
    default: return;
    }
}
// exe 0x4f3620: toward a random point (+-0x80)
void workerWander(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (actor.step == 0) {
        npc.bits = 0;
        actor.step = 1;
        actor.anim = 1;
        uint32_t k;
        do k = crtRand() & 6;
        while (k == npc.wanderPoint);
        npc.wanderPoint = uint8_t(k);
        npc.targetX = wanderPoints[k];
        npc.targetZ = wanderPoints[k + 1];
        npc.targetX += (crtRand() & 0xff) - 0x80u;
        npc.targetZ += (crtRand() & 0xff) - 0x80u;
    } else if (actor.step != 1) {
        return;
    }
    // exe 0x5e3bc0 from the target (its integer parts; the exe's stack vector has 0 in the low words) to the NPC
    const int32_t tx = int32_t(uint32_t(npc.targetX) << 16), tz = int32_t(uint32_t(npc.targetZ) << 16);
    actor.rotation.y = int16_t(atanAngle(float(int32_t(uint32_t(actor.pos.x) - uint32_t(tx))),
                                         float(int32_t(uint32_t(actor.pos.z) - uint32_t(tz)))) &
                               kAngleMask);
    integrateVelocity(actor, 0, 0, kWanderSpeed); // exe 0x5e3690
    if (!npcAway(actor, npc)) actor.enterState(uint8_t(NpcState::Main));
}
// exe 0x573a60: lifted -> carried
void liftedCheck(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (playerLiftTargetCheck(actor, 0)) {
        actor.enterState(uint8_t(NpcState::Carried));
        npc.bits |= kNpcNoHitEntry;
        actor.runFlags |= kRunFlagLifted;
    }
}
// exe 0x4f33a0: variant 0x81 (the worker): its sub-state, the hit test, the collision, the player's lift
void worker81(AnimatedActor& actor, CarriedNpcFields& npc) {
    switch (WorkerSub(actor.subState)) {
    case WorkerSub::Idle: workerIdle(actor, npc); break;
    case WorkerSub::Falling: workerFalling(actor, npc); break;
    case WorkerSub::Hit: hitReaction(actor, npc, true); break;
    case WorkerSub::Wander: workerWander(actor, npc); break;
    default: notPortedAt(kCarriedNpcPort, "variant 0x81 sub-state", actor.subState);
    }
    if (!(npc.bits & kNpcNoHitReaction) && npcHit(actor, npc)) {
        actor.subState = uint8_t(WorkerSub::Hit);
        actor.enterStep(0);
    }
    if (npcCollide(actor, npc) != 0 && !(npc.bits & kNpcNoFall)) {
        actor.subState = uint8_t(WorkerSub::Falling);
        actor.airborne = 1;
        actor.enterStep(0);
    }
    liftedCheck(actor, npc);
}
// exe 0x4f3200 (variant 0x80 sub 0; also variant 7)
void idlerIdle(AnimatedActor& actor, CarriedNpcFields& npc) {
    switch (actor.step) {
    case 0:
        npc.bits = kNpcNoFall;
        actor.step = 1;
        actor.anim = 0x20;
        break;
    case 1:
        if (actor.animKey.flags & kKeyJump) {
            actor.step = 2;
            npc.timer = uint16_t((crtRand() & 0x7f) + 0x20);
        }
        break;
    case 2:
        --npc.timer;
        if (npc.timer == 0) {
            actor.step = 1;
            actor.prevAnim = 0xff;
        }
        break;
    default: break;
    }
    idleSound(actor, npc, true);
}
// exe 0x4f3190: variant 0x80 (the idler): its sub-state, the hit test, the collision, the player's lift
void idler80(AnimatedActor& actor, CarriedNpcFields& npc) {
    switch (IdlerSub(actor.subState)) {
    case IdlerSub::Idle: idlerIdle(actor, npc); break;
    case IdlerSub::Hit: hitReaction(actor, npc, false); break;
    default: notPortedAt(kCarriedNpcPort, "variant 0x80 sub-state", actor.subState);
    }
    if (!(npc.bits & kNpcNoHitReaction) && npcHit(actor, npc)) {
        actor.subState = uint8_t(IdlerSub::Hit);
        actor.enterStep(0);
    }
    npcCollide(actor, npc);
    liftedCheck(actor, npc);
}

// ---- the script cast variants ([0x896e10 + v * 4]) ------------------------------------------------------------------
bool castCue(CarriedNpcFields& npc) { // the script changed the cue
    if (npc.cue == npc.lastCue) return false;
    npc.lastCue = npc.cue;
    return true;
}
// exe 0x5e33d0 / 0x5e33f0
void castTracks(AnimatedActor& actor, uint8_t seq0, uint8_t seq1) {
    CarriedNpcFields& c = classState<CarriedNpcFields>(actor);
    c.faceTracks.first.select(seq0, 0);
    c.faceTracks.second.select(seq1, 0);
}
// exe 0x4f3710: cast variant 0: its animations at each cue of the script
void cast0(AnimatedActor& actor, CarriedNpcFields& npc) {
    switch (actor.subState) {
    case 0:
        if (castCue(npc)) {
            actor.anim = 7;
            actor.subState = 1;
        }
        return;
    case 1:
        if (castCue(npc)) {
            actor.subState = 2;
            actor.anim = 8;
            castTracks(actor, 3, 2);
        }
        return;
    case 2:
        if (castCue(npc)) {
            actor.subState = 3;
            actor.anim = 7;
            actor.rotation.y = 0x160;
        }
        return;
    default: return;
    }
}
// exe 0x4f37b0: cast variant 1: its animations at each cue of the script
void cast1(AnimatedActor& actor, CarriedNpcFields& npc) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
    case 5:
        if (castCue(npc)) {
            actor.anim = s == 0 ? 10 : 0xf;
            actor.subState = uint8_t(s + 1u);
            castTracks(actor, 4, 3);
        }
        return;
    case 1:
    case 2:
        if (castCue(npc)) {
            actor.anim = s == 1 ? 0xb : 0xc;
            actor.subState = uint8_t(s + 1u);
        }
        return;
    case 3:
        if (actor.animKey.flags & kKeyJump) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0xd;
            integrateVelocity(actor, 0, 0, -0x180);
        }
        return;
    case 4:
        integrateVelocity(actor, 0, 0, -0x30);
        if (castCue(npc)) {
            actor.anim = 0xe;
            ++actor.subState;
        }
        return;
    default: return;
    }
}
// exe 0x4f38e0: cast variant 2: its animations at each cue of the script
void cast2(AnimatedActor& actor, CarriedNpcFields& npc) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
    case 1:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = s == 0 ? 0x11 : 0x12;
            castTracks(actor, s == 0 ? 4 : 0, s == 0 ? 3 : 0);
        }
        return;
    case 2:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0x13;
        }
        return;
    case 3:
        if (actor.animKey.flags & kKeyJump) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0x14;
        }
        return;
    case 4: integrateVelocity(actor, 0, 0, -0xa0); return;
    default: return;
    }
}
// exe 0x4f39d0 (script 0x25's cast): walks until the cue
void cast3(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (actor.subState != 0) return;
    integrateVelocity(actor, 0, 0, -0xa0); // exe 0x5e3690
    if (!castCue(npc)) return;
    actor.anim = 0x15;
    ++actor.subState;
}
// exe 0x4f3a10: cast variant 4: its animations at each cue of the script
void cast4(AnimatedActor& actor, CarriedNpcFields& npc) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0x17;
        }
        return;
    case 1:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0x18;
            actor.parts[12].flags &= uint8_t(0x7fu);
            actor.parts[3].flags |= 0x80u;
            classState<CarriedNpcFields>(actor).faceTracks.second.select(4, 0); // exe 0x5e33f0
            classState<CarriedNpcFields>(actor).faceTracks.first.select(5, 0);  // exe 0x5e33d0
        }
        return;
    case 2: // the player's hand-over animation reaches its step 4
        if (player().body.anim == kPlayerAnimGive && uint8_t(player().body.key.frame) == kPlayerAnimGiveStep) {
            actor.subState = uint8_t(s + 1u);
            npc.timer = 4;
            actor.anim = 0x19;
        }
        return;
    case 3: {
        const uint16_t t = npc.timer;
        npc.timer = uint16_t(t - 1u);
        if (t == 2) ++actor.subState;
        actor.prevAnim = 0xff;
        npc.animHold = uint8_t(t);
        return;
    }
    case 4:
        npc.animHold = kHoldFrame;
        if (castCue(npc)) {
            npc.animHold = 0;
            ++actor.subState;
            actor.anim = 0x19;
        }
        return;
    case 5:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 0x1a;
        }
        return;
    case 6:
        if (actor.animKey.flags & kKeyJump) {
            actor.subState = uint8_t(s + 1u);
            actor.parts[12].flags |= 0x80u;
            actor.parts[3].flags &= uint8_t(0x7fu);
        }
        return;
    case 7:
        if (castCue(npc)) {
            actor.anim = 0x1b;
            actor.subState = uint8_t(s + 1u);
        }
        return;
    default: return;
    }
}
// exe 0x4f3bc0: cast variant 5: its animations at each cue of the script
void cast5(AnimatedActor& actor, CarriedNpcFields& npc) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
        if (castCue(npc)) {
            actor.anim = 1;
            actor.subState = uint8_t(s + 1u);
        }
        return;
    case 1:
        integrateVelocity(actor, 0, 0, -0x90);
        if (actor.pos.z.whole > 0xa0) {
            actor.anim = 2;
            actor.pos.z.whole = 0xa0;
            ++actor.subState;
        }
        return;
    case 2:
    case 3:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = s == 2 ? 3 : 4;
            castTracks(actor, 1, s == 2 ? 1 : 0);
        }
        return;
    case 4:
        if (castCue(npc)) {
            actor.subState = uint8_t(s + 1u);
            actor.anim = 5;
            npc.scriptSignal = 1;
            classState<CarriedNpcFields>(actor).faceTracks.first.select(2, 0);
        }
        return;
    default: return;
    }
}
// exe 0x4f3cf0: walks (animation 1, 0xf0 a tick) on the ground
void cast6(AnimatedActor& actor) {
    if (actor.subState == 0) {
        actor.anim = 1;
        actor.subState = 1;
    } else if (actor.subState != 1) {
        return;
    }
    integrateVelocity(actor, 0, 0, -0xf0);
    actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
}
// exe 0x4f3130: the behaviour of its variant (0x80 / 0x81 / a cast member)
void npcMain(AnimatedActor& actor, CarriedNpcFields& npc) {
    const uint8_t v = actor.variant;
    if (!(v & kWorkerVariant)) {
        switch (v) { // [0x896e10 + v * 4]
        case 0: cast0(actor, npc); return;
        case 1: cast1(actor, npc); return;
        case 2: cast2(actor, npc); return;
        case 3: cast3(actor, npc); return;
        case 4: cast4(actor, npc); return;
        case 5: cast5(actor, npc); return;
        case 6: cast6(actor); return;
        case 7: idlerIdle(actor, npc); return;
        default: notPortedAt(kCarriedNpcPort, "variant", v);
        }
        return;
    }
    actor.tint = npc.damageTaken > 0x3b ? uint16_t(0x2108) : kTintNeutral;
    switch (v & 0x7f) { // [0x896e08 + (v & 0x7f) * 4]
    case 0: idler80(actor, npc); break;
    case 1: worker81(actor, npc); break;
    default: notPortedAt(kCarriedNpcPort, "variant", v);
    }
}
// exe 0x4f3060: the model, the hit points, the boxes; the behaviour by variant
void npcInit(AnimatedActor& actor, CarriedNpcFields& npc) {
    actor.clearClassBlock();
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);                      // exe 0x5e1490
        return;
    }
    const uint8_t v = actor.variant;
    actor.flags |= kActorDrawn;
    actor.prevAnim = 0xff;
    actor.collisionBox.pointAt(carriedNpcBox.stateAddress());
    if (!(v & kWorkerVariant)) {
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.anim = castAnims[v];
        CarriedNpcFields& c = classState<CarriedNpcFields>(actor);
        c.faceTracks.init(stateAddress(TableId::CarriedNpcCastTracks0), stateAddress(TableId::CarriedNpcCastTracks1)); // exe 0x5e33a0 / 0x5e33d0 / 0x5e33f0
        c.faceTracks.first.select(0, 0);
        c.faceTracks.second.select(0, 0);
    } else {
        actor.interactionBox.pointAt(carriedNpcLiftBox.stateAddress());
        actor.interaction = kInteractLift;
        actor.anim = workerAnims[v & 0x7f];
    }
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.enterState(uint8_t(NpcState::Main));
    npcMain(actor, npc);
}
// exe 0x4f3eb0: thrown / dropped, lands
void npcFlying(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (hitBoxOf(actor).result & kHitThrown) {
        npc.bits |= kNpcNoHitEntry;
        balanceAdd(-4 * int(statsOf(actor).balanceLossThrown)); // exe 0x5e51d0
    }
    if (npcFall(actor, npc, actor.rotation.y) != 0) {
        ++actor.subState;
        actor.enterStep(0);
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.airborne = 0;
        npc.bits &= uint8_t(~kNpcAttackEntry);
        soundPlayAtPos(kSoundLand, actor.pos);
    }
    npcCollide(actor, npc);
}
// exe 0x4f3d60: at the hands until let go
void npcInHands(AnimatedActor& actor, CarriedNpcFields& npc) {
    const bool idler = (actor.variant & 0x7f) == 0;
    if (actor.step == 0) {
        npc.bits = kNpcNoHitEntry;
        actor.anim = idler ? 0x24 : 0x2a;
        actor.enterStep(1);
        npc.timer = 0;
        soundPlayAtPos(kSoundLifted, actor.pos);
    }
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased();
    if (r == CarryRelease::Held) {
        const uint16_t t = npc.timer;
        npc.timer = uint16_t(t - 1u);
        if (t == 0) {
            npc.timer = uint16_t((crtRand() & 0x3f) + 0x2d);
            soundPlayAtPos(kSoundCarried, actor.pos);
        }
        return;
    }
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x80, -0x200, v); // exe 0x5e3e50
        actor.anim = idler ? 0x25 : 0x2b;
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole - 0x28));
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        npc.bits = kNpcAttackEntry | kNpcBounces;
    } else { // dropped
        actor.velocity.y = -0x80;
        actor.velocity.z = -0x40;
        npc.bits = kNpcBounces;
    }
    actor.acceleration.y = kGravity;
    actor.airborne = 1;
    actor.actorHit = 0;
    npc.collisionBits = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.enterStep(0);
    ++actor.subState;
    npcFlying(actor, npc);
}
// exe 0x4f3f50: the landing animation, then state 1
void npcLanded(AnimatedActor& actor, CarriedNpcFields& npc) {
    if (actor.step == 0) {
        npc.bits = kNpcNoHitEntry | kNpcNoHitReaction;
        actor.anim = (actor.variant & 0x7f) == 0 ? 0x23 : 0x29;
        actor.enterStep(1);
    } else if (actor.animKey.flags & kKeyJump) {
        actor.enterState(uint8_t(NpcState::Main));
    }
}
// exe 0x4f2ee0's body: the state, the animation, the hit entry, the collision queues
void carriedNpc(AnimatedActor& actor) {
    CarriedNpcFields& npc = classState<CarriedNpcFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (NpcState(actor.state)) {
    case NpcState::Init: npcInit(actor, npc); break;
    case NpcState::Main: npcMain(actor, npc); break;
    case NpcState::Carried:
        switch (CarriedSub(actor.subState)) {
        case CarriedSub::InHands: npcInHands(actor, npc); break;
        case CarriedSub::Flying: npcFlying(actor, npc); break;
        case CarriedSub::Landed: npcLanded(actor, npc); break;
        default: notPortedAt(kCarriedNpcPort, "state 2 sub-state", actor.subState);
        }
        break;
    default: notPortedAt(kCarriedNpcPort, "state", actor.state);
    }
    if (actor.flags & kActorLive) {
        if (!(actor.variant & kWorkerVariant)) { // exe 0x5e34f0
            CarriedNpcFields& c = classState<CarriedNpcFields>(actor);
            textureTracksTick(actor, c.faceTracks);
        }
        if (actor.anim == actor.prevAnim) {
            if (!(npc.animHold & kHoldFrame)) {
                animStepPose(actor); // exe 0x5e1e80
                npc.animHold = 0;
            }
        } else {
            animSetPose(actor, actor.anim, npc.animHold); // exe 0x5e1c30
            npc.animHold = 0;
        }
        if (actor.variant & kWorkerVariant) {
            const uint8_t b = npc.bits;
            const AnimatedActorStats stats = statsOf(actor);
            auto&& hit = hitBoxOf(actor);
            hit.result = 0;
            hit.previousX = actor.prevPos.x.whole;
            hit.previousZ = actor.prevPos.z.whole;
            if (!(b & kNpcNoHitEntry)) {
                const ShortVec4& point = bonePoint(actor, 0, kCarriedNpcPort); // exe 0x5e1810
                const uint32_t lo = uint32_t(stats.hitSize >> 2) | uint32_t(actor.slot) << 24;
                const int16_t pos[3] = {point.x, point.y, point.z};
                if (!(b & kNpcAttackEntry))
                    hitListSubmit(pos, hit,
                                  lo | hitGroup(kHitGroupLiftable) | kHitCompareHeights | kHitNoLockOn |
                                      kHitContactMotion,
                                  kHitAttackHarmless | kHitAttackSolid | kHitAttackBump);
                else
                    hitListSubmit(pos, static_cast<HitBoxHead&>(hit), lo | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn,
                                  uint32_t(stats.attack) | kHitAttackByPlayer);
            }
        }
        // exe 0x5f20f0(a, 0x30, 0x100, 0) unless state 2 sub 0: the shadow list entry (render output)
    }
    actorCollisionSubmitMover(actor); // exe 0x4140b0
}

} // namespace

void carriedNpcUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    carriedNpc(animatedActorAt(address));
}

} // namespace od::port
