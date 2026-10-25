// The boss of the pirates' mine (ST2B, story 12): the cannon turret (pool 0x20 type 0x14 variant 0, exe 0x473320;
// hypothesis names). States [0x890444]: 0 bind 0x473610 (the hit points of its row x 4, the boss gauge 0x415d30), 1 run
// 0x4736d0 (its sub-states [0x890450]: 0 idle 0x473790, 1 aimed shells 0x473800, 2 a flat volley 0x473930 (no
// sub-state ever selects it), 3 high lobs 0x473aa0), 2 death 0x473bd0 (0 blasts / the reward 0x473bf0, 1 the turret
// falls off 0x473e90). Every run tick the barrel's yaw (accelerated, braked, settled: exe 0x473f30 with 0x474120 /
// 0x474190) and pitch (exe 0x4740a0 with 0x4741c0) chase their targets; the shells leave through the muzzle (exe
// 0x474230, the shell spawner 0x4742d0). Hurt below half its hit points it smokes. The shadow is render output.
#include "content/saul_kada/st2b_cannon_boss.hpp"
#include "content/saul_kada/st2b_cannon_boss_state.hpp"

#include "content/saul_kada/dropped_item.hpp"
#include "content/saul_kada/pirate_servbots.hpp"
#include "content/saul_kada/st2b_cannon_shell.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/message.hpp"
#include "game/message_triggers.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st2b_cannon_boss_state;

namespace {

enum class BossState : uint8_t { Bind = 0, Run = 1, Death = 2 };
enum class BossSub : uint8_t { Idle = 0, Aimed = 1, Volley = 2, Lob = 3 };
enum class DeathSub : uint8_t { Blasts = 0, Fall = 1 };
enum class YawMover : uint8_t { Still = 0, Aim = 1, Accelerate = 2, Brake = 3, Settle = 4 };
constexpr uint16_t kHurtSound = 0x8d, kStunSound = 0x8e, kTurnSound = 0xae, kPitchSound = 0xc5;
constexpr int16_t kBarrelSpeedLimit = 0x60;
constexpr uint32_t kFlagBossBeaten = 0x5cc;
constexpr uint16_t kExplosionTint = 0x421f;
constexpr uint8_t kSmokeHalfVariant = 6, kSmokeQuarterVariant = 0;
// the dropped item and the message of its death
constexpr uint8_t kItemVariant = 1;
constexpr uint16_t kItemFlag = 0x3eb;
constexpr int8_t kItemMessage = 0x32;
constexpr uint8_t kTriggerVariant = 0xe;
constexpr int16_t kTriggerMessage = 0x12, kTriggerParam = 0x12c, kTriggerFlag = 0x5cd;
constexpr int8_t kTriggerStory = 0xc;
constexpr uint32_t kFallenDrop = 0x1900000; // 0x190 above it (16.16)

AnimatedActorStats bossStats(const AnimatedActor& actor) { return animatedStats(actor.type); }
int barrelAngle(const AnimatedActor& actor, const BossFields& boss) { return int(actor.rotation.y) + boss.barrelYaw; }
void positionValues(const Fixed16Vec3& pos, int32_t out[3]) {
    out[0] = pos.x;
    out[1] = pos.y;
    out[2] = pos.z;
}

// exe 0x474370(point, variant, life): a puff of smoke (the stage's spark emitter) at the s16 point
void smokePuff(const int16_t point[3], uint8_t variant, int16_t life) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& smoke = staticActorAt(slot);
    smoke.variant = variant;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.flags = kActorLive;
    smoke.type = kSparkEmitterStageType;
    smoke.pos.x.whole = point[0];
    smoke.pos.y.whole = point[1];
    smoke.pos.z.whole = point[2];
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = life;
}
// exe 0x474230: the muzzle: bone 2 + Ry Rx(the pitch, its yaw + the barrel's) * (0, -0x200, -0x1000) << 12
void muzzle(const AnimatedActor& actor, const BossFields& boss, int32_t out[3]) {
    const int16_t rot[3] = {boss.pitch, int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(boss.barrelYaw))), 0};
    int32_t v[3];
    rotateVector(rot, 0, -0x200, -0x1000, v); // exe 0x5e3970
    const ShortVec4* bone = bonePointOrNull(actor, 2); // exe 0x5e1810 (a null result would crash the exe)
    if (!bone) notPortedAt(kStoryPort, "exe 0x5e1810 returned null, bone", 2);
    const ShortVec4& point = *bone;
    const int16_t at[3] = {point.x, point.y, point.z};
    for (size_t k = 0; k < 3; ++k) out[k] = int32_t((uint32_t(int32_t(at[k])) << 16) + uint32_t(v[k]));
}
// exe 0x4742d0(actor, point, vy, vz, variant): a shell at the point, its speeds, the barrel's pitch / yaw; the camera
// shaken at the turret
void fireShell(const AnimatedActor& actor, const BossFields& boss, const int32_t point[3], int16_t vy, int16_t vz,
               CannonShellVariant variant) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shell = projectileActorAt(slot);
    shell.variant = uint8_t(variant);
    shell.flags = kActorLive;
    shell.type = kCannonShellType;
    shell.loopFlags |= kActorSweepGroup2;
    shell.pos.x = point[0];
    shell.pos.y = point[1];
    shell.pos.z = point[2];
    shell.velocity.x = 0;
    shell.velocity.y = vy;
    shell.velocity.z = int16_t(-vz);
    shell.rotation.x = boss.pitch;
    shell.rotation.y = int16_t(uint16_t(uint16_t(boss.barrelYaw) + uint16_t(actor.rotation.y)));
    shell.rotation.z = 0;
    cameraShakeAt(0x1400, 0x400, 0x80, 0x1000, actor.pos);
}
// exe 0x474120(actor, block, target): the barrel's yaw += speed >> 4, the speed += its acceleration (within +-0x60);
// true once the difference left to the target has the acceleration's opposite sign (passed it)
bool yawAccelerate(AnimatedActor& actor, BossFields& boss, int target) {
    boss.barrelYaw = int16_t(uint16_t(uint16_t(boss.barrelYaw) + uint16_t(actor.spin.y >> 4)));
    actor.spin.y = int16_t(uint16_t(uint16_t(actor.spin.y) + uint16_t(boss.yawAccel)));
    if (actor.spin.y > kBarrelSpeedLimit) actor.spin.y = kBarrelSpeedLimit;
    if (actor.spin.y < -kBarrelSpeedLimit) actor.spin.y = int16_t(-kBarrelSpeedLimit);
    const int d = angleDiff(uint32_t(barrelAngle(actor, boss)), uint32_t(target));
    return ((uint32_t(d) ^ uint16_t(boss.yawAccel)) & 0x8000) != 0;
}
// exe 0x474190(actor, block): the barrel's yaw += speed >> 4, the speed -= its acceleration; true once the speed's sign
// leaves the acceleration's
bool yawBrake(AnimatedActor& actor, BossFields& boss) {
    boss.barrelYaw = int16_t(uint16_t(uint16_t(boss.barrelYaw) + uint16_t(actor.spin.y >> 4)));
    actor.spin.y = int16_t(uint16_t(uint16_t(actor.spin.y) - uint16_t(boss.yawAccel)));
    return ((uint32_t(uint16_t(actor.spin.y)) ^ uint16_t(boss.yawAccel)) & 0x8000) != 0;
}
// exe 0x4741c0(actor, block, target): the pitch's speed +-0x60 toward the target, the pitch += speed >> 4; true once
// passed
bool pitchStep(AnimatedActor& actor, BossFields& boss, int target) {
    const int d = angleDiff(uint32_t(int32_t(boss.pitch)), uint32_t(target));
    if (d > 0) actor.spin.x = kBarrelSpeedLimit;
    if (d < 0) actor.spin.x = int16_t(-kBarrelSpeedLimit);
    boss.pitch = int16_t(uint16_t(uint16_t(boss.pitch) + uint16_t(actor.spin.x >> 4)));
    const int d2 = angleDiff(uint32_t(int32_t(boss.pitch)), uint32_t(target));
    return ((uint32_t(uint16_t(actor.spin.x)) ^ uint32_t(d2)) & 0x8000) != 0;
}
// exe 0x473f30: the barrel's yaw mover: aim (within 0x20: done, else an acceleration of +-0x10 toward the player),
// accelerate until past the player's angle (sound 0xae every 0x14 ticks; that angle kept), brake (the acceleration
// flipped), settle back onto the kept angle
void moveYaw(AnimatedActor& actor, BossFields& boss) {
    switch (YawMover(boss.yawMover)) {
    case YawMover::Aim: {
        const int angle = angleOf(player().pos, actor.pos);
        const int d = angleDiff(uint32_t(barrelAngle(actor, boss)), uint32_t(angle));
        if (uint32_t(d + 0x20) <= 0x40u) {
            boss.yawMover = uint8_t(YawMover::Still);
            boss.turnSoundTicks = 0;
            return;
        }
        boss.yawAccel = int16_t(d > 0 ? 0x10 : 0xfff0);
        boss.yawMover = uint8_t(YawMover::Accelerate);
        boss.turnSoundTicks = 0;
        return;
    }
    case YawMover::Accelerate: {
        const int angle = angleOf(player().pos, actor.pos);
        yawAccelerate(actor, boss, angle);
        const int d = angleDiff(uint32_t(barrelAngle(actor, boss)), uint32_t(angle));
        if (uint32_t(d + 0x20) <= 0x40u) {
            boss.yawTarget = int16_t(angle);
            boss.yawMover = uint8_t(YawMover::Brake);
        }
        if (boss.turnSoundTicks == 0) {
            soundPlayAtPos(kTurnSound, actor.pos);
            boss.turnSoundTicks = 0x14;
        } else {
            boss.turnSoundTicks -= 1u;
        }
        return;
    }
    case YawMover::Brake:
        if (yawBrake(actor, boss)) {
            boss.yawMover = uint8_t(YawMover::Settle);
            boss.yawAccel = int16_t(-int(uint16_t(boss.yawAccel)));
        }
        return;
    case YawMover::Settle: {
        const int16_t target = boss.yawTarget;
        if (yawAccelerate(actor, boss, target)) {
            boss.yawMover = uint8_t(YawMover::Still);
            boss.barrelYaw = int16_t(uint16_t(target - uint16_t(actor.rotation.y)));
        }
        return;
    }
    default: return;
    }
}
// exe 0x4740a0: the pitch mover: toward its target (sound 0xc5 every 0x14 ticks), snapped onto it once passed
void movePitch(AnimatedActor& actor, BossFields& boss) {
    switch (boss.pitchMoving) {
    case 0: boss.pitchSoundTicks = 0; return;
    case 1:
        if (pitchStep(actor, boss, boss.pitchTarget)) {
            boss.pitchMoving = 0;
            boss.pitch = boss.pitchTarget;
            return;
        }
        if (boss.pitchSoundTicks == 0) {
            soundPlayAtPos(kPitchSound, actor.pos);
            boss.pitchSoundTicks = 0x14;
        } else {
            boss.pitchSoundTicks -= 1u;
        }
        return;
    default: return;
    }
}
// the barrel aimed: its pitch and its yaw both settled
bool settled(const BossFields& boss) { return boss.yawMover == 0 && boss.pitchMoving == 0; }

// ---- the run's sub-states [0x890450] -------------------------------------------------------------------------------
// exe 0x473790: idle until the player is in front (within 0x200 of its yaw) nearer than 0x1800, or it was hit
void idle(AnimatedActor& actor, const BossFields& boss) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
    if (uint32_t(d + 0x200) <= 0x400u && planarDistance(actor.pos, player().pos) < 0x1800) {
        actor.subState = uint8_t(BossSub::Aimed);
        actor.restartStep();
    }
    if (boss.provoked) {
        actor.subState = uint8_t(BossSub::Aimed);
        actor.restartStep();
    }
}
// exe 0x473800: pitched 0xff00 and aimed, then an aimed shell every 0x3c ticks; the player within 0x800 -> the lobs
void aimed(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        if (uint16_t(boss.pitch) != 0xff00) {
            boss.pitchTarget = int16_t(0xff00);
            boss.pitchMoving = 1;
        }
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        boss.yawMover = uint8_t(YawMover::Aim);
        actor.enterStep(2);
        [[fallthrough]];
    case 2:
        if (!settled(boss)) return;
        actor.enterStep(3);
        return;
    case 3: {
        int32_t point[3], target[3];
        muzzle(actor, boss, point);
        positionValues(player().pos, target);
        int16_t vy, vz;
        lobVelocity(point, target, boss.pitch, 0x20, 0xffffffffu, vy, vz);
        fireShell(actor, boss, point, vy, vz, CannonShellVariant::Aimed);
        boss.timer = 0x3c;
        actor.enterStep(4);
        [[fallthrough]];
    }
    case 4:
        boss.timer -= 1u;
        if (boss.timer == 0) actor.enterStep(1);
        if (planarDistance(actor.pos, player().pos) < 0x800) {
            actor.subState = uint8_t(BossSub::Lob);
            actor.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x473930: pitched 0x100, then 3 flat shells (vz 0x600) 0x78 ticks apart; after the third -> the lobs (the exe
// compares the angle difference, not the distance it computes, with 0x4b0 .. 0x834, so it always switches); the player
// at x >= -0x400 -> the aimed shells. No sub-state selects it.
void volley(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        boss.pitchTarget = 0x100;
        boss.pitchMoving = 1;
        actor.step = 1;
        actor.stepCounter = 3;
        [[fallthrough]];
    case 1:
        boss.yawMover = uint8_t(YawMover::Aim);
        actor.step = 2;
        [[fallthrough]];
    case 2:
        if (!settled(boss)) return;
        actor.step = 3;
        return;
    case 3: {
        int32_t point[3];
        muzzle(actor, boss, point);
        fireShell(actor, boss, point, 0, 0x600, CannonShellVariant::Flat);
        boss.timer = 0x78;
        actor.step = 4;
        if (actor.stepCounter) actor.stepCounter -= 1u;
        [[fallthrough]];
    }
    case 4:
        boss.timer -= 1u;
        if (boss.timer == 0) actor.step = 1;
        if (actor.stepCounter == 0) {
            const int d = angleDiff(uint32_t(barrelAngle(actor, boss)), uint32_t(angleOf(player().pos, actor.pos)));
            (void)planarDistance(actor.pos, player().pos); // computed and dropped by the exe
            if (uint32_t(d + 0x20) > 0x40u || d > 0x834 || d < 0x4b0) {
                actor.subState = uint8_t(BossSub::Lob);
                actor.restartStep();
            }
        }
        if (player().pos.x.whole >= -0x400) {
            actor.subState = uint8_t(BossSub::Aimed);
            actor.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x473aa0: pitched 0xfc80 (steep), then a high lob (its reach at most 0x960) every 0x78 ticks; the player beyond
// 0x960 -> the aimed shells
void lob(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        boss.pitchTarget = int16_t(0xfc80);
        boss.pitchMoving = 1;
        boss.lobFired = 1;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        boss.yawMover = uint8_t(YawMover::Aim);
        actor.enterStep(2);
        [[fallthrough]];
    case 2:
        if (!settled(boss)) return;
        actor.enterStep(3);
        return;
    case 3: {
        int32_t point[3], target[3];
        muzzle(actor, boss, point);
        positionValues(player().pos, target);
        int16_t vy, vz;
        lobVelocity(point, target, boss.pitch, 0x20, 0x960, vy, vz);
        fireShell(actor, boss, point, vy, vz, CannonShellVariant::High);
        boss.timer = 0x78;
        actor.enterStep(4);
        [[fallthrough]];
    }
    case 4:
        boss.timer -= 1u;
        if (boss.timer == 0) actor.step = 1;
        if (planarDistance(actor.pos, player().pos) > 0x960) {
            actor.subState = uint8_t(BossSub::Aimed);
            actor.restartStep();
        }
        return;
    default: return;
    }
}

// ---- the death (exe 0x473bd0) --------------------------------------------------------------------------------------
// exe 0x473bf0: a defeat counted, flag 0x5cc, window 4 closed; 0x28 ticks of blasts every 4th tick around the turret;
// then the item, a message trigger (message 0x5cd), a big blast, a Servbot, its drops; the fall
void deathBlasts(AnimatedActor& actor, BossFields& boss) {
    actor.tint = kTintNeutral;
    switch (actor.step) {
    case 0:
        actor.enterStep(1);
        boss.timer = 0x28;
        countDefeat();                      // exe 0x5e53e0
        setStoryFlag(kFlagBossBeaten);      // exe 0x510e10
        windowClose(4, WindowClose::Force); // exe 0x5576b0(4, 1)
        [[fallthrough]];
    case 1: {
        if ((boss.timerLow & 3) == 0) {
            if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
                StaticActor& blast = staticActorAt(slot);
                blast.flags = kActorLive;
                blast.type = typeByte(GlobalStaticType::Blast);
                blast.variant = 0;
                blast.loopFlags |= kActorSweepGroup2;
                rngStep();
                const uint32_t r = rngStep();
                int32_t o[3];
                rotateByYaw(int((r & 0xff) << 4), 0, -int((r & 0xf) << 9), -0x1000, o); // exe 0x5e38d0
                blast.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
                blast.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
                blast.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
                const uint32_t r2 = rngStep();
                BlastParams params(actorHeaderAt(slot));
                params.attack = 1;
                params.size = uint16_t(((r2 & 3) + 4) << 4);
                actor.tint = kExplosionTint;
            }
        }
        const int16_t left = int16_t(boss.timer);
        if (left < 0x19) boss.dying = 1;
        boss.timer = uint16_t(left - 1);
        if (uint16_t(left - 1) == 0) actor.enterStep(2);
        return;
    }
    case 2: {
        const uint32_t itemSlot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!itemSlot) return;
        StaticActor& item = staticActorAt(itemSlot);
        DroppedItemParams itemParams(staticActorAt(itemSlot));
        item.flags = kActorLiveDrawn;
        item.loopFlags |= kActorSweepGroup2;
        item.type = kDroppedItemType;
        item.subtype = 0;
        item.variant = kItemVariant;
        item.pos.x = actor.pos.x;
        item.pos.y = int32_t(uint32_t(actor.pos.y) - kFallenDrop);
        item.pos.z = actor.pos.z;
        itemParams.flag = kItemFlag;
        itemParams.kind = 0;
        itemParams.message = kItemMessage;
        actor.enterStep(3);
        if (const uint32_t triggerSlot = allocActorE0()) { // exe 0x5e1440
            ControllerActor& trigger = controllerActorAt(triggerSlot);
            MessageTriggerFields& triggerFields = classState<MessageTriggerFields>(trigger);
            trigger.flags = kActorLive;
            trigger.type = kMessageTriggerType;
            trigger.variant = kTriggerVariant;
            triggerFields.message = kTriggerMessage;
            triggerFields.lastStory = kTriggerStory;
            triggerFields.params[0] = kTriggerParam;
            triggerFields.flag = kTriggerFlag;
        }
        [[fallthrough]];
    }
    case 3:
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(slot);
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = 1;
            blast.pos.x = actor.pos.x;
            blast.pos.y = int32_t(uint32_t(actor.pos.y) - kFallenDrop);
            blast.pos.z = actor.pos.z;
            BlastParams params(actorHeaderAt(slot));
            params.size = 0x200;
            params.attack = 1;
        }
        if (const uint32_t slot = allocActor20()) { // exe 0x5e1160: a Servbot jumps out
            AnimatedActor& servbot = animatedActorAt(slot);
            servbot.flags = kActorLive;
            servbot.loopFlags |= kActorSweepGroup2;
            servbot.type = kPirateServbotType;
            servbot.subtype = kServbotOnFootSubtype;
            servbot.variant = 0;
            servbot.pos.x = actor.pos.x;
            servbot.pos.y = int32_t(uint32_t(actor.pos.y) - kFallenDrop);
            servbot.pos.z = actor.pos.z;
        }
        spawnDrops(actor.pos, bossStats(actor).dropTable); // exe 0x5f1270
        actor.subState = uint8_t(DeathSub::Fall);
        actor.restartStep();
        return;
    default: return;
    }
}
// exe 0x473e90: the turret drops off (vy -0x400, gravity 0x40), pitching down 0x80 a tick, blinking; freed once its
// bone 2 is below y 0
void deathFall(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        actor.enterStep(1);
        actor.airborne = 1;
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.velocity.y = int16_t(0xfc00);
        actor.parts[0].flags |= kPartHidden;
        actor.parts[1].flags |= kPartHidden;
        [[fallthrough]];
    case 1: {
        boss.pitch = int16_t(uint16_t(uint16_t(boss.pitch) - 0x80u));
        actor.flags ^= kActorDrawn;
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        actor.velocity.y += 0x40u;
        actorGround(env, actor); // exe 0x40f190 (its result dropped)
        const ShortVec4* bone = bonePointOrNull(actor, 2);
        if (!bone) notPortedAt(kStoryPort, "exe 0x5e1810 returned null, bone", 2);
        if (bone->y > 0) freeActor(actor); // exe 0x5e1490
        return;
    }
    default: return;
    }
}
void death(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) { // exe 0x473bd0
    if (DeathSub(actor.subState) == DeathSub::Blasts) deathBlasts(actor, boss);
    else deathFall(env, actor, boss);
}

// ---- the states [0x890444] -----------------------------------------------------------------------------------------
// exe 0x473610: its model, the hit points of its row x 4, its box, the barrel's pose, on the ground, the boss gauge
void bind(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(uint32_t(bossStats(actor).hitPoints) << 2) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.collisionBox.pointAt(TableId::St2bCannonBossBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    boss.pitch = int16_t(0xff00);
    boss.barrelYaw = int16_t(0xfd00);
    actor.pos.y.whole = int16_t(uint16_t(actorGround(env, actor))); // exe 0x40f190
    hudAddBossGauge(actor, 0, 2, 0x80ff20);                        // exe 0x415d30
    actor.enterState(uint8_t(BossState::Run));
}
// exe 0x4736d0
void run(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    const uint32_t hit = boss.hitBox.result;
    if (boss.invulnerable != 0) {
        boss.invulnerable -= 1u;
    } else if (hit & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        if (hit & kHitAttackStrongPush) {
            boss.invulnerable = 0x14;
            soundPlayAtPos(kStunSound, actor.pos);
        } else {
            soundPlayAtPos(kHurtSound, actor.pos);
        }
        boss.provoked = 1;
        if (hitPointsAdd(actor, -int(hit & kHitDamageMask))) { // exe 0x5e52f0
            actor.enterState(uint8_t(BossState::Death));
            death(env, actor, boss);
            return;
        }
    }
    switch (BossSub(actor.subState)) {
    case BossSub::Idle: idle(actor, boss); break;
    case BossSub::Aimed: aimed(actor, boss); break;
    case BossSub::Volley: volley(actor, boss); break;
    case BossSub::Lob: lob(actor, boss); break;
    default: notPortedAt(kStoryPort, "0x4736d0 sub-state", actor.subState);
    }
    moveYaw(actor, boss);
    movePitch(actor, boss);
    actor.pos.y.whole = int16_t(uint16_t(actorGround(env, actor))); // exe 0x40f190
}
// its hit capsules: the body (r 0x28, y -0x64 .. -0x20d) and the barrel (r 0x25, 0x1000 along the barrel's yaw, y
// -0x1f4 .. -0x2e4); dying: the body not a lock-on target
void submitHits(const AnimatedActor& actor, BossFields& boss) {
    const int16_t x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    int16_t pa[3] = {x, int16_t(uint16_t(y - 0x64)), z}, pb[3] = {x, int16_t(uint16_t(y - 0x20d)), z};
    boss.hitBox.result = 0;
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    const uint32_t owner = hitOwner(actor.slot);
    hitListSubmitCapsule(pa, 0x28, pb, 0x28, boss.hitBox,
                         (boss.dying ? kHitNoLockOn : 0u) | owner | hitGroup(kHitGroupBodies) | hitCapsule(1),
                         kHitAttackSolid); // exe 0x5e4190
    int32_t o[3];
    rotateByYaw(barrelAngle(actor, boss), 0, 0, 0x1000, o); // exe 0x5e38d0
    const int16_t ox = int16_t(uint16_t(uint32_t(o[0] >> 16))), oz = int16_t(uint16_t(uint32_t(o[2] >> 16)));
    pa[0] = int16_t(uint16_t(pa[0] + ox));
    pb[0] = int16_t(uint16_t(pb[0] + ox));
    pa[2] = int16_t(uint16_t(pa[2] + oz));
    pb[2] = int16_t(uint16_t(pb[2] + oz));
    pb[1] = int16_t(uint16_t(actor.pos.y.whole - 0x2e4));
    pa[1] = int16_t(uint16_t(actor.pos.y.whole - 0x1f4));
    hitListSubmitCapsule(pa, 0x25, pb, 0x25, boss.hitBox,
                         owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1), kHitAttackSolid);
}
// below half its hit points, every 16th tick: smoke from 2 (variant 6) or, below a quarter, 3 (variant 0) of the
// barrel's smoke points
void smoke(const AnimatedActor& actor, const BossFields& boss) {
    const int16_t hp = actor.hitPoints, maxHp = actor.maxHitPoints;
    if (!(hp < int16_t(maxHp >> 1) && (gameState().frame & 0xf) == 0)) return;
    int n;
    uint8_t variant;
    if (hp < int16_t(maxHp >> 2)) {
        n = 3;
        variant = kSmokeQuarterVariant;
    } else {
        n = 2;
        variant = kSmokeHalfVariant;
    }
    for (uint32_t t = stateAddress(TableId::St2bCannonBossSmokePoints); n != 0; --n, t += 8) {
        const ShortVec3 offset = dataShortVec3(t);
        int32_t o[3];
        rotateByYaw(barrelAngle(actor, boss), int(offset.x) << 4, int(offset.y) << 4, int(offset.z) << 4, o);
        const int16_t point[3] = {int16_t(uint16_t((o[0] >> 16) + actor.pos.x.whole)),
                                  int16_t(uint16_t((o[1] >> 16) + actor.pos.y.whole)),
                                  int16_t(uint16_t((o[2] >> 16) + actor.pos.z.whole))};
        smokePuff(point, variant, 0x10);
    }
}
void cannonBoss(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x473320
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BossState(actor.state)) {
    case BossState::Bind: bind(env, actor, boss); break;
    case BossState::Run: run(env, actor, boss); break;
    case BossState::Death: death(env, actor, boss); break;
    default: notPortedAt(kStoryPort, "0x473320 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    // the barrel's pitch / yaw onto the decoded frame's entries 3 (x) / 2 (y)
    LaneVec4 pitchEntry = decodedFrame().entries[3];
    pitchEntry.x = int16_t(uint16_t(uint16_t(boss.pitch) + uint16_t(pitchEntry.x)));
    LaneVec4 yawEntry = decodedFrame().entries[2];
    yawEntry.y = int16_t(uint16_t(uint16_t(boss.barrelYaw) + uint16_t(yawEntry.y)));
    animPose(actor); // exe 0x5e2d90
    submitHits(actor, boss);
    smoke(actor, boss);
    if (uint16_t(actor.state | actor.subState << 8) != uint8_t(BossState::Death)) {
        // the shadow (exe 0x5f20f0(actor, 0x120, 0x100, 0)) is render output
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
    }
}

} // namespace

void cannonBossUpdate(const PlayerEnv& env, uint32_t actor) { cannonBoss(env, animatedActorAt(actor)); }

} // namespace od::port
