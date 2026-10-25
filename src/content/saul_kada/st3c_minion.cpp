// ST3C (the pirate base, story 12): the gold-statue fight's minion (pool 0x20 ST3C type 0x78, exe 0x4ff7d0;
// hypothesis names), spawned up to 3 at a time near the player by the fight's spawner (content/saul_kada/st3c_minion_spawner: its
// point, its yaw, its byte of the spawner's slot block).
//
// The class top keeps its block's address in the exe's global 0xa59e98, the player's angle and planar distance seen
// from it; states [0x897b98]: 0 bind 0x4ff870, 1 run 0x4ff960, 2 freed 0x468fa0.
// The run: a hit (exe 0x500890: an attack of the player's / thrown / strong with damage -> sound 0x8d, the damage unless
// kMinionNoDamage; dead -> dying), its off-screen bit (exe 0x5e5a50), its sub-state [0x897ba4], its animation (the
// decoded frame's entry 2 z turned by the frame counter), then its hit entry unless kMinionNoHit.
// Sub-states: 0 rise 0x4ffa10 (from 0x100 below the floor, pitched back, its parts 0 / 2 hidden until within 0x80),
// a pause of 0xf, pitched upright -> 1; 1 wait 0x4ffb50 -> 2; 2 / 3 chase 0x4ffb90 / 0x4ffc80 (the same code): turned
// to the player, then crawling (-0x80) turning: within 0x600 -> 5, blocked -> 4; 4 blocked 0x4ffd70: backing off along
// the escape yaw, turning, until within 0x400 of the player's yaw or its timer runs out -> 2; 5 bob 0x4fff90 for 0x14
// ticks turning -> 6 (or 7 when the player has no ground below him); 6 charge 0x5000d0 (-0x200 at the player, a random
// offset off screen), braking when 0x400 off (-> 2); a wall (sound 0x8d, the yaw reflected + 0x800, damage 0x18) -> 8
// (dead -> 9), a body's touch -> 9; 7 leap 0x500350 at the player (gravity 0x14, its pitch along the arc after 0x14
// ticks), blocked or touched -> 9; 8 bounced back 0x500570 (braking 0x34) -> 1, a wall -> 9; 9 death 0x500660: a blast
// (static global type 7, size 0x100, killed by damage: the attack of its row's byte 2) and a Servbot (type 0xb variant
// 3, its gone flag the block's byte) unless the last push-out failed; hidden; once the Servbot is gone its spawner's
// slot byte cleared, freed.
// Dust (pool 0xa0 global effect 0 variant 7, the hit spark): exe 0x5009c0 radius 0x40, life 0x80..0x9f; exe 0x500a90
// radius 0x10, life 0x40..0x4f. Deviation: exe 0x5e40c0's 4th word (stack garbage, stored as the point's yaw) is 0.
// No shadow in this class.
#include "content/saul_kada/st3c_minion.hpp"
#include "content/saul_kada/st3c_minion_state.hpp"

#include "content/saul_kada/pirate_servbots.hpp"
#include "content/saul_kada/pirate_servbots_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

using namespace od::port::st3c_minion_state;

namespace od::port {

using namespace st3c_minion_state;

namespace {

enum class MinionState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class MinionSub : uint8_t {
    Rise = 0, Wait = 1, Chase = 2, ChaseAgain = 3, Blocked = 4, Bob = 5, Charge = 6, Leap = 7, Bounce = 8, Death = 9
};
constexpr uint8_t kMinionNoHit = 0x01;  // no hit entry (rising / dying)
constexpr uint8_t kMinionKilled = 0x02; // dying by damage (its blast takes its row's attack)
constexpr uint8_t kMinionNoDamage = 0x04;
// the exe's global holding the running minion's block
const DataTable<ScalarTable<uint32_t, 4>> voiceOffsets{TableId::St3cMinionVoiceOffsets}; // {0, 1, 2, 0}: the sounds' id offsets
constexpr uint32_t kServbotGoneOffset = 0x160; // its Servbot-gone byte in its slot (the shown-parts byte)
constexpr uint16_t kHurtSound = 0x8d, kChargeSound = 0x2f3, kCrySound = 0x2ff, kDeathSound = 0x302;
constexpr uint32_t kHitKinds = kHitAttackBump | kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong; // 0x1d0000
constexpr uint8_t kDustVariant = 7;
constexpr size_t kHiddenPartA = 0, kHiddenPartB = 2;
constexpr uint8_t kServbotVariant = 3;

AnimatedActorStats minionStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void move(AnimatedActor& actor, int x, int y, int z) { moveAlongRotation(actor.pos, actor.rotation, x, y, z); } // exe 0x5e37b0
int ground(const PlayerEnv& env, const Actor& actor) { return actorGround(env, actor); }               // exe 0x40f190
uint16_t randomVoice(uint16_t base) { return uint16_t(voiceOffsets[rngStep() & 3] + base); }
void positionValues(const AnimatedActor& actor, int32_t out[3]) {
    out[0] = actor.pos.x;
    out[1] = actor.pos.y;
    out[2] = actor.pos.z;
}
// exe 0x5e40c0(angle, r) into the projection scratch's point: {dx, 0, dz, 0}
void scratchVector(int angle, int r) {
    int16_t dx, dz;
    angleVector(angle, r, dx, dz);
    LaneVec4& point = projectionScratch().point;
    point.x = dx;
    point.y = 0;
    point.z = dz;
    projectionScratch().pointYaw = 0;
}
// the end of a sub-state: dying (no hit, killed) -> the death
void toDeath(AnimatedActor& actor, MinionFields& minion) {
    minion.flagsLow |= uint8_t(kMinionNoHit | kMinionKilled);
    actor.subState = uint8_t(MinionSub::Death);
    actor.restartStep();
}
void enterSub(AnimatedActor& actor, MinionSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}

// ---- small callees -----------------------------------------------------------------------------------------------
// exe 0x5007b0(actor, mask): the ground followed; its wall hit = the push-out's result; 0 on the ground, 1 started
// falling, 2 otherwise (blocked: the position restored when the push-out failed)
int groundFollow(const PlayerEnv& env, AnimatedActor& actor, uint32_t mask) {
    actor.wallHit = 0;
    if (actor.airborne == 0) {
        const int16_t point[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int g = probeGroundAt(env, point, actor.collisionBox,
                                    int(actor.pos.y.whole) - int(actor.prevPos.y.whole), mask); // exe 0x40f1c0
        const int y = actor.pos.y.whole;
        if (uint32_t(g - y + 0x3c) <= 0x78) {
            actor.pos.y = int32_t(uint32_t(g) << 16);
            return 0;
        }
        if (y <= g) {
            actor.airborne = 1;
            return 1;
        }
        const uint32_t result = actorPushOutMask(env, actor, mask); // exe 0x410980
        actor.wallHit = result;
        if (int32_t(result) < 0) actor.pos = actor.prevPos;
        return 2;
    }
    const uint32_t result = actorPushOutMask(env, actor, mask); // exe 0x410980
    actor.wallHit = result;
    if (int32_t(result) < 0) {
        actor.pos = actor.prevPos;
        return 2;
    }
    if (result & kWallOnYPlus) actor.airborne = 0;
    return 2;
}
// exe 0x500890: the hit; 0 none, 1 hit, -1 killed
int takeHit(AnimatedActor& actor, const MinionFields& minion) {
    if (minion.flagsLow & kMinionNoHit) return 0;
    const uint32_t result = minion.hitBox.result;
    const uint32_t damage = result & kHitDamageMask;
    if (!(result & kHitKinds) || damage == 0) return 0;
    actor.tint = kTintFlash;
    soundPlayAtPos(kHurtSound, actor.pos);
    if (!(minion.flagsLow & kMinionNoDamage) && hitPointsAdd(actor, -int(damage)) != 0) return -1; // exe 0x5e52f0
    return 1;
}
// exe 0x500900(actor, 0): its hit entry at (x, y - 0x40, z) (through the projection scratch's point)
void submitHit(const AnimatedActor& actor, MinionFields& minion) {
    minion.hitBox.previousX = actor.prevPos.x.whole;
    minion.hitBox.previousZ = actor.prevPos.z.whole;
    minion.hitBox.result = 0;
    const uint32_t attack = kHitAttackSolid | kHitAttackThrown | minionStats(actor).attack;
    LaneVec4& point = projectionScratch().point;
    point.x = actor.pos.x.whole;
    point.y = int16_t(uint16_t(actor.pos.y.whole) - 0x40u);
    point.z = actor.pos.z.whole;
    const int16_t at[3] = {point.x, point.y, point.z};
    hitListSubmit(at, minion.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x50),
                  attack); // exe 0x5e4120
}
// the body of exe 0x5009c0 / 0x500a90: a puff of dust (the hit spark's variant 7) at a random point r away on the
// floor, its life (rng & mask) + base
void dustPuff(const PlayerEnv& env, const AnimatedActor& actor, int r, uint32_t mask, uint32_t base) {
    scratchVector(int(rngStep() & 0xfff), r); // exe 0x5e40c0
    const uint32_t slot = allocActorA0(0);     // exe 0x5e13b0
    if (!slot) return;
    EffectActor& dust = effectActorAt(slot);
    dust.flags = kActorLive;
    dust.type = typeByte(GlobalEffectType::HitSpark);
    dust.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    dust.variant = kDustVariant;
    const int shift = stage().coordinateShift & 31;
    const LaneVec4& offset = projectionScratch().point;
    dust.pos.x = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.x.whole) + uint16_t(offset.x)) << shift));
    dust.pos.y = int16_t(uint16_t(uint32_t(ground(env, actor))));
    dust.pos.z = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.z.whole) + uint16_t(offset.z)) << shift));
    classState<HitSparkFields>(effectActorAt(slot)).size = uint16_t((rngStep() & mask) + base);
}
void wideDust(const PlayerEnv& env, const AnimatedActor& actor) { dustPuff(env, actor, 0x40, 0x1f, 0x80); } // 0x5009c0
void dust(const PlayerEnv& env, const AnimatedActor& actor) { dustPuff(env, actor, 0x10, 0xf, 0x40); }      // 0x500a90
// the target of the charge / the leap: the player, offset at random when it is off screen
void chargeTarget(const AnimatedActor& actor, int32_t out[3]) {
    const Player& p = player();
    if (actor.runFlags & kActorOffScreen) {
        const uint32_t r = rngStep();
        out[0] = int32_t(uint32_t(p.pos.x) + (((r & 0x1ff) - 0x100u) << 16));
        out[1] = p.pos.y;
        out[2] = int32_t(uint32_t(p.pos.z) + ((((r >> 9) & 0x1ff) - 0x100u) << 16));
    } else {
        out[0] = p.pos.x;
        out[1] = p.pos.y;
        out[2] = p.pos.z;
    }
}

// ---- the sub-states [0x897ba4] -----------------------------------------------------------------------------------
// exe 0x4ffa10 (its steps [0x4ffb34])
void rise(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    switch (actor.step) {
    case 0:
        minion.timer = 0;
        actor.velocity.z = int16_t(0xff60);
        ++actor.step;
        [[fallthrough]];
    case 1: {
        move(actor, 0, 0, actor.velocity.z);
        const int g = ground(env, actor);
        const int y = actor.pos.y.whole;
        if (y <= g) {
            actor.pos.y = int32_t(uint32_t(g) << 16);
            ++actor.step;
        } else if (y - g < 0x80) {
            actor.parts[kHiddenPartA].flags &= uint8_t(~kPartHidden);
            actor.parts[kHiddenPartB].flags &= uint8_t(~kPartHidden);
        }
        wideDust(env, actor);
        wideDust(env, actor);
        return;
    }
    case 2:
        minion.timer += 1u;
        if (minion.timer == 0xf) {
            actor.spin.x = 0x40;
            ++actor.step;
        }
        return;
    case 3:
        actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint16_t(actor.spin.x)));
        if (actor.rotation.x < 0) return;
        actor.rotation.x = 0;
        minion.timer = uint16_t((rngStep() & 0x1f) + 0x1e);
        minion.flags &= uint16_t(~uint16_t(kMinionNoHit));
        enterSub(actor, MinionSub::Wait);
        return;
    default: return;
    }
}
// exe 0x4ffb50
void wait(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    if (actor.step == 0) {
        minion.timer -= 1u;
        if (minion.timer == 0) enterSub(actor, MinionSub::Chase);
    }
    dust(env, actor);
}
// exe 0x4ffb90 (sub-state 2) / 0x4ffc80 (sub-state 3): the same code
void chase(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    const uint32_t towardPlayer = uint32_t(int32_t(minion.playerYaw));
    switch (actor.step) {
    case 0:
        actor.velocity.z = int16_t(0xff80);
        actor.spin.y = 0x30;
        ++actor.step;
        [[fallthrough]];
    case 1: {
        // abs(exe 0x5e3df0(...)) with the call evaluated twice (the exe's abs macro)
        const int first = turnYawTo(actor.rotation.y, towardPlayer, actor.spin.y);
        int second = turnYawTo(actor.rotation.y, towardPlayer, actor.spin.y);
        if (first < 0) second = -second;
        if (second <= actor.spin.y) ++actor.step;
        break;
    }
    case 2: {
        turnYawTo(actor.rotation.y, towardPlayer, actor.spin.y);
        move(actor, 0, 0, actor.velocity.z);
        const int follow = groundFollow(env, actor, 2);
        if (minion.playerDistance <= 0x600) enterSub(actor, MinionSub::Bob);
        if (follow == 2) enterSub(actor, MinionSub::Blocked);
        break;
    }
    default: break;
    }
    dust(env, actor);
}
// exe 0x4ffd70 (its steps [0x4fff74])
void blocked(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    switch (actor.step) {
    case 0:
        actor.velocity.z = int16_t(0xff80);
        minion.timer = uint16_t((rngStep() & 0xf) + 0xf);
        ++actor.step;
        [[fallthrough]];
    case 1: {
        move(actor, 0, 0, actor.velocity.z);
        int32_t moved[3]; // the position after the move (the exe's stack copy)
        positionValues(actor, moved);
        if (groundFollow(env, actor, 2) != 2) {
            actor.step = 3;
            break;
        }
        minion.escapeYaw = int16_t(angleOf(moved[0], moved[2], actor.pos.x, actor.pos.z));
        if (angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(minion.escapeYaw))) < 0)
            actor.spin.y = 0x30;
        else actor.spin.y = int16_t(0xffd0);
        ++actor.step;
        break;
    }
    case 2:
        move(actor, 0, 0, actor.velocity.z);
        if (groundFollow(env, actor, 2) == 2) {
            actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y)));
        } else {
            actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) - uint16_t(actor.spin.y)));
            ++actor.step;
        }
        break;
    case 3: {
        move(actor, 0, 0, actor.velocity.z);
        if (groundFollow(env, actor, 2) == 2) minion.timer = uint16_t((rngStep() & 0xf) + 0xf);
        int d = angleDiff(uint32_t(int32_t(minion.escapeYaw)), uint32_t(int32_t(minion.playerYaw)));
        minion.timer -= 1u;
        if (minion.timer == 0) {
            enterSub(actor, MinionSub::Chase);
        } else {
            if (d < 0) d = -d;
            if (d > 0x400) enterSub(actor, MinionSub::Chase);
        }
        break;
    }
    default: break;
    }
    dust(env, actor);
}
// exe 0x4fff90
void bob(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    const uint32_t towardPlayer = uint32_t(int32_t(minion.playerYaw));
    switch (actor.step) {
    case 0:
        actor.spin.y = 0x30;
        minion.timer = 0x14;
        actor.stepCounter = 0;
        ++actor.step;
        [[fallthrough]];
    case 1: {
        const int g = ground(env, actor);
        actor.pos.y = int32_t(uint32_t(g) << 16);
        ++actor.stepCounter;
        if (actor.stepCounter & 1) actor.pos.y = int32_t((uint32_t(g) << 16) + 0x40000u);
        // the exe's abs macro on a comparison: exe 0x5e3df0 called twice, the flag (0 / 1) of the second call
        // compared with the turn step (the negated branch is unreachable: the first flag is never negative)
        const int step = actor.spin.y;
        const int first = turnYawTo(actor.rotation.y, towardPlayer, step) <= step ? 1 : 0;
        int c;
        if (first >= 0) c = turnYawTo(actor.rotation.y, towardPlayer, step) <= actor.spin.y ? 1 : 0;
        else c = -(turnYawTo(actor.rotation.y, towardPlayer, step) <= actor.spin.y ? 1 : 0);
        if (minion.timer != 0) minion.timer -= 1u;
        if (c <= actor.spin.y && minion.timer == 0) {
            actor.pos.y = int32_t(uint32_t(ground(env, actor)) << 16);
            if (ground(env, player()) < 0) enterSub(actor, MinionSub::Leap); // exe 0x40f190 on the player
            else enterSub(actor, MinionSub::Charge);
        }
        break;
    }
    default: break;
    }
    dust(env, actor);
}
// exe 0x5000d0
void charge(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    const uint32_t towardPlayer = uint32_t(int32_t(minion.playerYaw));
    switch (actor.step) {
    case 0: {
        soundPlayAtPos(kChargeSound, actor.pos);
        soundPlayAtPos(randomVoice(kCrySound), actor.pos);
        actor.spin.y = 0x30;
        actor.velocity.z = 0;
        actor.acceleration.x = 0;
        actor.acceleration.z = int16_t(0xffec);
        int32_t target[3];
        chargeTarget(actor, target);
        actor.rotation.y = int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z));
        ++actor.step;
        [[fallthrough]];
    }
    case 1: {
        actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z)));
        const int16_t v = actor.velocity.z;
        if (v != 0 && v > -0x200) actor.velocity.z = int16_t(0xfe00);
        move(actor, 0, 0, actor.velocity.z);
        int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), towardPlayer);
        if (d < 0) d = -angleDiff(uint32_t(int32_t(actor.rotation.y)), towardPlayer);
        if (d > 0x400) {
            minion.timer -= 1u;
            if (minion.timer == 0) {
                actor.acceleration.z = 0x34;
                ++actor.step;
            }
        } else {
            minion.timer = 0xf;
        }
        break;
    }
    case 2:
        actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z)));
        if (actor.velocity.z < 0) {
            actor.velocity.z = 0;
            enterSub(actor, MinionSub::Chase);
        }
        move(actor, 0, 0, actor.velocity.z);
        break;
    default: break;
    }
    if (groundFollow(env, actor, 2) == 2) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        reflectRotationOffWalls(actor.rotation, actor.wallHit); // exe 0x410740
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u));
        enterSub(actor, MinionSub::Bounce);
        if (!(minion.flagsLow & kMinionNoDamage) && hitPointsAdd(actor, -0x18) != 0) toDeath(actor, minion); // 0x5e52f0
    } else if (minion.hitBox.result & kHitAttackBump) {
        toDeath(actor, minion);
    }
    dust(env, actor);
}
// exe 0x500350
void leap(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    const uint8_t step = actor.step;
    if (step == 0 || step == 2 || step == 3) {
        if (step == 0) {
            soundPlayAtPos(kChargeSound, actor.pos);
            soundPlayAtPos(randomVoice(kCrySound), actor.pos);
            actor.spin.y = 0x30;
            actor.velocity.z = int16_t(0xfe00);
            actor.acceleration.x = 0;
            actor.acceleration.y = 0x14;
            actor.acceleration.z = int16_t(0xffec);
            actor.step = 1;
            actor.stepCounter = 0;
            minion.timer = 0x14;
        }
        if (step != 3) { // exe 0x5003ee (step 2, and step 0 falls into it): aimed at the player
            int32_t target[3];
            const Player& p = player();
            if (actor.runFlags & kActorOffScreen) {
                const uint32_t r = rngStep();
                target[0] = int32_t(uint32_t(p.pos.x) + (((r & 0x1ff) - 0x100u) << 16));
                target[1] = int32_t(uint32_t(p.pos.y) + ((((r >> 9) & 0x1ff) - 0x180u) << 16));
                target[2] = int32_t(uint32_t(p.pos.z) + ((((r >> 0x12) & 0x1ff) - 0x100u) << 16));
            } else {
                target[0] = p.pos.x;
                target[1] = int32_t(uint32_t(p.pos.y) + 0xff800000u);
                target[2] = p.pos.z;
            }
            actor.rotation.y = int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z));
            int32_t from[3];
            positionValues(actor, from);
            actor.velocity.y = int16_t(launchSpeed(from, target, -int(actor.velocity.z), actor.acceleration.y,
                                                   0xffffffffu)); // exe 0x5e3ee0
            actor.airborne = 1;
            minion.timer = 1;
            ++actor.step;
        }
        // exe 0x5004d5 (step 3, and the steps above fall into it): the arc, pitched along it once the timer is out
        actor.rotation.x = 0;
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        move(actor, 0, actor.velocity.y, actor.velocity.z);
        if (minion.timer != 0) minion.timer -= 1u;
        else actor.rotation.x = int16_t(pitchOf(actor.prevPos, actor.pos) + 0x800); // exe 0x5e3ca0
        if (actorTestMove(env, actor) != 0) toDeath(actor, minion); // exe 0x40f570
    }
    if (minion.hitBox.result & kHitAttackBump) toDeath(actor, minion);
}
// exe 0x500570
void bounce(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    switch (actor.step) {
    case 0:
        actor.acceleration.z = int16_t(0xffcc);
        actor.velocity.z = int16_t(-int(actor.velocity.z));
        ++actor.step;
        [[fallthrough]];
    case 1: {
        const int16_t v = actor.velocity.z;
        if (v != 0) {
            const int16_t n = int16_t(uint16_t(int(actor.acceleration.z) + v));
            actor.velocity.z = n;
            if (n <= 0) actor.velocity.z = 0;
        } else {
            minion.timer = uint16_t((rngStep() & 0xf) + 0x1e);
            enterSub(actor, MinionSub::Wait);
        }
        move(actor, 0, 0, actor.velocity.z);
        if (groundFollow(env, actor, 2) == 2) {
            actor.tint = kTintFlash;
            soundPlayAtPos(kHurtSound, actor.pos);
            toDeath(actor, minion);
        }
        break;
    }
    default: break;
    }
    dust(env, actor);
}
// exe 0x500660
void death(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    switch (actor.step) {
    case 0: {
        groundFollow(env, actor, 2);
        soundPlayAtPos(randomVoice(kDeathSound), actor.pos);
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(slot);
            BlastParams params(actorHeaderAt(slot));
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = 0;
            blast.spawnByte = 0;
            blast.pos.x = actor.pos.x;
            blast.pos.y = actor.pos.y;
            params.size = 0x100;
            blast.pos.z = actor.pos.z;
            if (minion.flagsLow & kMinionKilled) params.attack = minionStats(actor).strongAttack;
        }
        if (int32_t(actor.wallHit) >= 0) {
            if (const uint32_t slot = allocActor20()) { // exe 0x5e1160
                AnimatedActor& servbot = animatedActorAt(slot);
                servbot.flags = kActorLive;
                servbot.loopFlags |= kActorSweepGroup2;
                servbot.type = kPirateServbotType;
                servbot.subtype = kServbotOnFootSubtype;
                servbot.variant = kServbotVariant;
                servbot.spawnByte = 0;
                servbot.pos = actor.pos;
                servbot.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) - 0x800u));
                PirateServbotParams(servbot).goneFlag = slotFieldWord(actor, kServbotGoneOffset);
                classState<pirate_servbots_state::ServbotFields>(servbot).goneFlag = &minion.servbotGone;
            }
        }
        actor.setDrawn(false);
        ++actor.step;
        return;
    }
    case 1:
        if (minion.servbotGone != 0) {
            *minion.spawnerSlot = 0; // (its spawn word: the byte's exe address)
            actor.enterState(uint8_t(MinionState::Free));
        }
        return;
    default: return;
    }
}

// ---- the states [0x897b98] ---------------------------------------------------------------------------------------
// exe 0x4ff870: the model, the hit points of its row, 0x100 below the floor, pitched back, its parts 0 / 2 hidden
void bind(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.prevAnim = 0;
    actor.anim = 0;
    const uint16_t hp = minionStats(actor).hitPoints;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0x30;
    actor.collisionBox.pointAt(TableId::St3cMinionBox);
    actor.actorHit = 0;
    actor.platform = nullptr;
    actor.clearClassBlock(); // exe 0x40dc40
    minion.flags = kMinionNoHit;
    actor.pos.y = int32_t(uint32_t(ground(env, actor) + 0x100) << 16);
    actor.parts[kHiddenPartB].flags |= kPartHidden;
    actor.rotation.x = int16_t(0xfc00);
    actor.parts[kHiddenPartA].flags |= kPartHidden;
    animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    actor.enterState(uint8_t(MinionState::Run));
}
// exe 0x4ff960
void run(const PlayerEnv& env, AnimatedActor& actor, MinionFields& minion) {
    if (takeHit(actor, minion) == -1) {
        minion.flagsLow |= kMinionNoHit;
        enterSub(actor, MinionSub::Death);
    }
    // exe 0x5e5a50(actor, 0): off the screen / far (unless kActorRunMarked)
    if (!(actor.runFlags & kActorRunMarked)) {
        if (pointVisibleOrNear(actor.pos, 0) != 0) actor.runFlags |= kActorOffScreen;
        else actor.runFlags &= uint8_t(~kActorOffScreen);
    }
    switch (MinionSub(actor.subState)) {
    case MinionSub::Rise: rise(env, actor, minion); break;
    case MinionSub::Wait: wait(env, actor, minion); break;
    case MinionSub::Chase:
    case MinionSub::ChaseAgain: chase(env, actor, minion); break;
    case MinionSub::Blocked: blocked(env, actor, minion); break;
    case MinionSub::Bob: bob(env, actor, minion); break;
    case MinionSub::Charge: charge(env, actor, minion); break;
    case MinionSub::Leap: leap(env, actor, minion); break;
    case MinionSub::Bounce: bounce(env, actor, minion); break;
    case MinionSub::Death: death(env, actor, minion); break;
    default:
        notPortedAt(kStoryPort, "0x4ff960 sub-state (table 0x897ba4 has 10 entries, then 0)", actor.subState);
    }
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    LaneVec4 turned = decodedFrame().entries[2]; // its body turned by the frame counter
    turned.z = int16_t(uint16_t(uint16_t(turned.z) + uint16_t(uint32_t(gameState().frame) << 6)));
    animPose(actor); // exe 0x5e2d90
    if (!(minion.flagsLow & kMinionNoHit)) submitHit(actor, minion);
    else minion.hitBox.result = 0;
}
void statueMinion(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4ff7d0
    MinionFields& minion = classState<MinionFields>(actor);
    st3cMinionGlobals().minionBlock = slotFieldWord(actor, kAnimatedClassBlockOffset); // its hit box's exe address
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    minion.playerYaw = int16_t(angleOf(player().pos, actor.pos));
    minion.playerDistance = uint16_t(planarDistance(actor.pos, player().pos)); // exe 0x5e3b00
    switch (MinionState(actor.state)) {
    case MinionState::Bind: bind(env, actor, minion); return;
    case MinionState::Run: run(env, actor, minion); return;
    case MinionState::Free: freeActor(actor); return; // 0x468fa0: exe 0x5e1490
    default:
        notPortedAt(kStoryPort, "0x4ff7d0 state (table 0x897b98 has 3 entries, then the sub-state table)", actor.state);
    }
}

} // namespace

void statueMinionUpdate(const PlayerEnv& env, uint32_t actor) { statueMinion(env, animatedActorAt(actor)); }

} // namespace od::port
