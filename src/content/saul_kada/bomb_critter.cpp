// The pirates' hovering bomb critter (ST48 pool 0x20 type 0x13, exe 0x472870; hypothesis names).
// States [0x8903fc]: 0 bind 0x472920 (it starts popping out of the ground), 1 run 0x472a00, 2 blow up 0x472b10.
// The run's sub-states [0x890408]: 0 wait for the player within 0xa00 (0x472c00), 1 hover after him (0x472c50), 2 hurt:
// puffing smoke (0x472d60), 3 carried / thrown (0x472e70), 4 popping out of the ground (0x472fd0). Hovering and hurt it
// faces away from the player, closes in to about 0xc0, bobs 0xe0 above the ground and runs its fuse; near the player
// for 0x180 ticks it lights the fuse (0x60 ticks, a beep every 8). Hits hurt it or kill it; the player can lift it.
// The blow-up (by its step): 0 killed (its drops, then two blasts), 1 the fuse ran out, 2 thrown and hit / landed.
// The shadow (exe 0x5f20f0) is render output.
#include "content/saul_kada/bomb_critter.hpp"
#include "content/saul_kada/bomb_critter_state.hpp"

#include "content/saul_kada/smoke_puff.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/skid_sparks.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace bomb_critter_state;

namespace {

enum class CritterState : uint8_t { Bind = 0, Run = 1, BlowUp = 2 };
enum class CritterSub : uint8_t { Wait = 0, Hover = 1, Hurt = 2, Carried = 3, PopUp = 4 };
enum class BlowUpCause : uint8_t { Killed = 0, FuseOut = 1, Crashed = 2 };
enum class CarriedStep : uint8_t { Lift = 0, Held = 1, Flying = 2 };
enum class HurtStep : uint8_t { Start = 0, Smoking = 1, Recover = 2 };
enum class PopUpStep : uint8_t { Burst = 0, Rise = 1 };
enum class CritterAnim : uint8_t { Hover = 0, PopUp = 1, Hurt = 2, Fuse = 3, Recover = 4 };
constexpr uint8_t kNoAnim = 0xff;
constexpr uint16_t kHurtSound = 0x8d, kBeepSound = 0xc1, kPopSound = 0xa3;
constexpr uint16_t kNearTicks = 0x180, kFuseTicks = 0x60;
constexpr int kWakeDistance = 0xa00, kFuseDistance = 0x400;
constexpr int16_t kGravity = 0x40;
constexpr uint16_t kFuseBlink = 0x1f; // the tint of the lit fuse's blink (red)
constexpr uint8_t kPopSparkVariant = 0x1e;
constexpr uint8_t kSmokeLife = 0x30;

void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void restartSub(AnimatedActor& actor, CritterSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void playAnim(AnimatedActor& actor, CritterAnim anim) {
    actor.anim = uint8_t(anim);
    actor.animKey.frame = 0;
}

// exe 0x4730c0: a hit of the player's with damage -> the white flash, the sound, its hit points less the damage;
// -1 dead, 1 hurt, 0 none
int takeHit(AnimatedActor& actor, const CritterFields& critter) {
    const uint32_t result = critter.hitBox.result;
    const uint32_t damage = result & kHitDamageMask;
    if (!(result & kHitAttackByPlayer) || damage == 0) return 0;
    actor.tint = kTintFlash;
    sound(kHurtSound, actor);
    return hitPointsAdd(actor, -int(damage)) != 0 ? -1 : 1; // exe 0x5e52f0
}
// exe 0x473120: the lit fuse ticks down (a beep every 8); out -> the blow-up; true then
bool fuseTick(AnimatedActor& actor, CritterFields& critter) {
    if (critter.fuseLit == 0) return false;
    critter.fuseTicks -= 1u;
    const uint16_t left = critter.fuseTicks;
    if (left == 0) {
        actor.enterStateKeepingStep(uint8_t(CritterState::BlowUp));
        actor.enterStep(uint8_t(BlowUpCause::FuseOut));
        return true;
    }
    if ((left & 7) == 0) sound(kBeepSound, actor);
    return false;
}
// exe 0x473170: the forward speed toward -0x300 (by 0x20) while farther than 0xc0 - speed from the player, else back
// toward 0; the planar distance to him (exe 0x5e3b00)
int approach(AnimatedActor& actor) {
    const int d = planarDistance(actor.pos, player().pos);
    const uint16_t speed = uint16_t(actor.velocity.z);
    if (d > 0xc0 - int(int16_t(speed))) {
        const uint16_t n = uint16_t(speed - 0x20u);
        actor.velocity.z = int16_t(n);
        if (int16_t(n) < -0x300) actor.velocity.z = int16_t(0xfd00);
    } else {
        const uint16_t n = uint16_t(speed + 0x20u);
        actor.velocity.z = int16_t(n);
        if (int16_t(n) > 0) actor.velocity.z = 0;
    }
    return d;
}
// exe 0x4731d0: hovering 0xe0 above the ground with a bob (the 64-step cosine of its phase); more than 0x10 off: 0x10
// toward it; then pushed out of the room (exe 0x410960)
void bob(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    critter.bobPhase = uint8_t((critter.bobPhase + 1u) & 0x3fu);
    const int ground = actorGround(env, actor); // exe 0x40f190
    const int32_t wave = (int32_t(trig64Tables().cosine[critter.bobPhase]) << 7) >> 0xc;
    const int32_t want = ground + wave - 0xe0;
    const int32_t diff = want - int32_t(actor.pos.y.whole);
    const int32_t distance = diff < 0 ? -diff : diff;
    if (distance > 0x10) {
        if (diff > 0) actor.pos.y = int32_t(uint32_t(actor.pos.y) + 0x100000u);
        else actor.pos.y = int32_t(uint32_t(actor.pos.y) + 0xfff00000u);
    } else {
        actor.pos.y = int32_t(uint32_t(want) << 16);
    }
    actorPushOut(env, actor);
}
// exe 0x473260: turned away from the player (0x40 a tick, snapped within 0x40)
void faceAway(AnimatedActor& actor) {
    const int away = angleOf(actor.pos, player().pos) - 0x800;                         // exe 0x5e3bc0
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(away)); // exe 0x5e3d80
    const int distance = d < 0 ? -d : d;
    if (distance > 0x40) {
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + (d > 0 ? 0x40u : 0xffc0u)));
        return;
    }
    actor.rotation.y = int16_t(away);
}
// exe 0x4732c0: a puff of smoke (the stage's pool-0x40 type 0xb) 0x20 below, along its yaw
void smoke(const AnimatedActor& actor) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& puff = projectileActorAt(slot);
    puff.flags = kActorLive;
    puff.loopFlags |= kActorSweepGroup2;
    puff.type = kSmokePuffType;
    puff.variant = 0;
    puff.pos.x = actor.pos.x;
    puff.pos.y = int32_t(uint32_t(actor.pos.y) + 0x200000u);
    puff.pos.z = actor.pos.z;
    puff.rotation.x = 0;
    puff.rotation.y = actor.rotation.y;
    puff.rotation.z = 0x100;
    SmokePuffParams(projectileActorAt(slot)).life = 0x30;
}
// the hover's and the hurt's move: forward at its speed, pushed out of the room
void glide(const PlayerEnv& env, AnimatedActor& actor) {
    integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    actorPushOut(env, actor);                         // exe 0x410960
}

// ---- the run's sub-states ([0x890408]) ------------------------------------------------------------------------------
// exe 0x472c00: the player within 0xa00 (or a hit) -> the hover; killed -> the blow-up
void wait(AnimatedActor& actor, const CritterFields& critter) {
    if (uint32_t(distance3(actor.pos, player().pos)) < uint32_t(kWakeDistance)) restartSub(actor, CritterSub::Hover);
    const int hit = takeHit(actor, critter);
    if (hit == 0) return;
    if (hit > 0) {
        restartSub(actor, CritterSub::Hover);
        return;
    }
    actor.enterState(uint8_t(CritterState::BlowUp));
}
// exe 0x472c50: after the player; near him for 0x180 ticks -> the fuse lit
void hover(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    faceAway(actor);
    switch (actor.step) {
    case 0:
        critter.nearTicks = kNearTicks;
        playAnim(actor, CritterAnim::Hover);
        ++actor.step;
        [[fallthrough]];
    case 1: glide(env, actor); break;
    default: break;
    }
    const int d = approach(actor);
    bob(env, actor, critter);
    if (fuseTick(actor, critter)) return;
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60: lifted
        restartSub(actor, CritterSub::Carried);
        return;
    }
    const int hit = takeHit(actor, critter);
    if (hit > 0) {
        if (critter.fuseLit == 0 && d < kFuseDistance) restartSub(actor, CritterSub::Hurt);
        return;
    }
    if (hit < 0) {
        actor.enterState(uint8_t(CritterState::BlowUp));
        return;
    }
    if (critter.fuseLit == 0 && d < kFuseDistance) {
        critter.nearTicks -= 1u;
        if (critter.nearTicks != 0) return;
        playAnim(actor, CritterAnim::Fuse);
        critter.fuseLit = 1;
        critter.fuseTicks = kFuseTicks;
        return;
    }
    critter.nearTicks = kNearTicks;
}
// exe 0x472d60: hurt: smoking for 0x30 ticks, then the recovery animation -> the hover
void hurt(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    faceAway(actor);
    switch (HurtStep(actor.step)) {
    case HurtStep::Start:
        playAnim(actor, CritterAnim::Hurt);
        critter.smokeTicks = kSmokeLife;
        ++actor.step;
        [[fallthrough]];
    case HurtStep::Smoking: {
        glide(env, actor);
        const uint8_t left = uint8_t(critter.smokeTicks - 1u);
        critter.smokeTicks = left;
        if (left == 0) {
            playAnim(actor, CritterAnim::Recover);
            ++actor.step;
        }
        break;
    }
    case HurtStep::Recover:
        glide(env, actor);
        if (actor.animKey.flags == kKeyHold) restartSub(actor, CritterSub::Hover);
        break;
    default: break;
    }
    approach(actor);
    bob(env, actor, critter);
    if (fuseTick(actor, critter)) return;
    if (!(critter.smokeTicks & 3) && actor.step < uint8_t(HurtStep::Recover)) smoke(actor);
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        restartSub(actor, CritterSub::Carried);
        return;
    }
    if (takeHit(actor, critter) < 0) actor.enterState(uint8_t(CritterState::BlowUp));
}
// exe 0x472e70: carried; thrown / dropped it tumbles until it hits something
void carried(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    switch (CarriedStep(actor.step)) {
    case CarriedStep::Lift:
        playAnim(actor, CritterAnim::Fuse);
        ++actor.step;
        [[fallthrough]];
    case CarriedStep::Held: {
        carryAtHands(actor, 0, 0, 0);                    // exe 0x573ab0
        const CarryRelease release = carryReleased(); // exe 0x573950
        if (release == CarryRelease::Held) break;
        if (release == CarryRelease::Thrown) {
            int32_t aim[3];
            playerAimVector(0, -0x180, -0x400, aim); // exe 0x5e3e50
            actor.velocity.x = 0;
            actor.velocity.y = int16_t(aim[1]);
            actor.velocity.z = int16_t(aim[2]);
        } else {
            actor.velocity.x = 0;
            actor.velocity.y = int16_t(0xff00);
            actor.velocity.z = int16_t(0xffd0);
        }
        actor.prevPos = actor.pos;
        actor.acceleration.y = kGravity;
        actor.airborne = 1;
        moveAlongRotation(actor.prevPos, actor.rotation, 0, 0, 0x2000); // exe 0x5e3810
        ++actor.step;
        [[fallthrough]];
    }
    case CarriedStep::Flying:
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + 0x180u));
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        if ((critter.hitBox.result & (kHitAttackDeflects | kHitAttackSolid)) ||
            int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
            actorPushOut(env, actor);
            actor.enterStateKeepingStep(uint8_t(CritterState::BlowUp));
            actor.enterStep(uint8_t(BlowUpCause::Crashed));
        }
        break;
    default: break;
    }
    fuseTick(actor, critter);
}
// exe 0x472fd0: a ring of 8 sparks at the ground, then up out of it -> the hover
void popUp(const PlayerEnv& env, AnimatedActor& actor) {
    switch (PopUpStep(actor.step)) {
    case PopUpStep::Burst: {
        const int ground = actorGround(env, actor); // exe 0x40f190
        for (uint32_t i = 0; i < 8; ++i) {
            const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
            if (!slot) continue;
            EffectActor& spark = effectActorAt(slot);
            spark.flags = kActorLive;
            spark.loopFlags |= kActorLateLoop;
            spark.setTypeWord(uint32_t(typeByte(GlobalEffectType::SkidSparks)) | uint32_t(kPopSparkVariant) << 16);
            spark.pos.x = actor.pos.x.whole;
            spark.pos.y = int16_t(ground);
            spark.pos.z = actor.pos.z.whole;
            const uint32_t r = rngStep();
            SkidSparkFields& fields = classState<SkidSparkFields>(effectActorAt(slot));
            fields.spareWord = 0;
            fields.yaw = uint16_t(((r & 0x3ff) + (i << 9)) & 0xfff);
        }
        actor.velocity.y = int16_t(0xfc00);
        sound(kPopSound, actor);
        actor.acceleration.y = kGravity;
        ++actor.step;
        return;
    }
    case PopUpStep::Rise: {
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        const int16_t vy = actor.velocity.y;
        if (vy >= 0) {
            actor.restartStep();
            actor.subState = uint8_t(CritterSub::Hover);
            return;
        }
        integrateVelocity(actor, 0, vy, 0);
        return;
    }
    default: return;
    }
}

// ---- the states ([0x8903fc]) ----------------------------------------------------------------------------------------
// exe 0x472920: the model, 0x80 above the ground, the hit points of its row, its boxes, liftable; popping up
void bind(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    critter.bobPhase = 0;
    const int ground = actorGround(env, actor); // exe 0x40f190
    actor.pos.y = int32_t(uint32_t(ground + 0x80) << 16);
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.anim = uint8_t(CritterAnim::PopUp);
    actor.prevAnim = kNoAnim;
    actor.animKey.frame = 0;
    const uint16_t hp = hasAnimatedStats(actor.type) ? animatedStats(actor.type, actor.variant).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::BombCritterBox);
    actor.interactionBox.pointAt(TableId::BombCritterLiftBox);
    actor.interaction = kInteractLift;
    critter.fuseLit = 0;
    critter.fuseTicks = 0;
    critter.nearTicks = 0;
    critter.hitBox.previousX = actor.prevPos.x.whole;
    critter.hitBox.previousZ = actor.prevPos.z.whole;
    critter.hitBox.result = 0;
    actor.restartStep();
    actor.state = uint8_t(CritterState::Run);
    actor.subState = uint8_t(CritterSub::PopUp);
}
// exe 0x472a00: off the grid -> freed; the sub-state; drawn: its hit entry (thrown: one of the player's attacks)
void run(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    if (gridCellUnder(actor.pos) == 0) { // exe 0x40f030
        freeActor(actor);
        return;
    }
    switch (CritterSub(actor.subState)) {
    case CritterSub::Wait: wait(actor, critter); break;
    case CritterSub::Hover: hover(env, actor, critter); break;
    case CritterSub::Hurt: hurt(env, actor, critter); break;
    case CritterSub::Carried: carried(env, actor, critter); break;
    case CritterSub::PopUp: popUp(env, actor); break;
    default: notPortedAt(kStoryPort, "0x472a00: sub-state past [0x890408]", actor.subState);
    }
    if (!(actor.flags & kActorDrawn)) return;
    critter.hitBox.previousX = actor.prevPos.x.whole;
    critter.hitBox.previousZ = actor.prevPos.z.whole;
    critter.hitBox.result = 0;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    uint32_t group, attack;
    if (CritterSub(actor.subState) == CritterSub::Carried) {
        group = hitGroup(kHitGroupPlayerAttacks);
        attack = kHitAttackByPlayer;
    } else {
        group = hitGroup(kHitGroupBodies);
        attack = actor.hitPoints < 0 ? kHitAttackBump : kHitAttackSolid;
    }
    const AnimatedActorStats stats = animatedStats(actor.type, actor.spawnByte);
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | group | hitRadius(0x30);
    hitListSubmit(pos, critter.hitBox, shape, uint32_t(stats.attack) | attack); // exe 0x5e4120
    if (critter.fuseLit != 0 && (critter.fuseTicksLow & 1)) actor.tint = kFuseBlink;
}
// exe 0x472b10: (killed: its drops) two blasts (sweep group 2, or 1 when it crashed), freed
void blowUp(AnimatedActor& actor) {
    uint8_t sweep;
    switch (BlowUpCause(actor.step)) {
    case BlowUpCause::Killed:
        spawnDrops(actor.pos, animatedStats(actor.type, actor.variant).dropTable); // exe 0x5f1270
        sweep = kActorSweepGroup2;
        break;
    case BlowUpCause::FuseOut: sweep = kActorSweepGroup2; break;
    case BlowUpCause::Crashed: sweep = kActorSweepGroup1; break;
    default: freeActor(actor); return;
    }
    struct Blast {
        uint8_t variant;
        uint16_t size, attack;
    };
    constexpr Blast kBlasts[2] = {{0, 0x100, 8}, {1, 0x200, 0x10}};
    for (const Blast& b : kBlasts) {
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (!slot) continue;
        StaticActor& blast = staticActorAt(slot);
        blast.flags = kActorLive;
        blast.loopFlags |= sweep;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = b.variant;
        blast.pos = actor.pos;
        BlastParams params(actorHeaderAt(slot));
        params.size = b.size;
        params.attack = b.attack;
    }
    freeActor(actor); // exe 0x5e1490
}

void bombCritter(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x472870
    CritterFields& critter = classState<CritterFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (CritterState(actor.state)) { // [0x8903fc]
    case CritterState::Bind: bind(env, actor, critter); break;
    case CritterState::Run: run(env, actor, critter); break;
    case CritterState::BlowUp: blowUp(actor); break;
    default: notPortedAt(kStoryPort, "0x472870: state past [0x8903fc]", actor.state);
    }
    if (actor.flags & kActorLive) {
        if (actor.anim == actor.prevAnim) animStepPose(actor);                       // exe 0x5e1e80
        else animSetPose(actor, actor.anim, int(int8_t(actor.animKey.frame))); // exe 0x5e1c30
    }
    // the shadow (exe 0x5f20f0(actor, 0x30, 0x100, 0) when drawn) is render output
}

} // namespace

void bombCritterUpdate(const PlayerEnv& env, uint32_t actor) { bombCritter(env, animatedActorAt(actor)); }

} // namespace od::port
