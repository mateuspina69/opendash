// The ST34 ruin's sentries (sub 3, story 12+; pool 0x20 type 0x15 sub 1, exe 0x474710; hypothesis names): dormant
// reaverbots that wake on the player's "use".
// States [0x89071c]: 0 bind 0x474920, 1 run 0x474a00, 2 (a null entry: the exe would crash), 3 dying 0x475710.
// The run: the neutral tint, the hit check (exe 0x475840, unless kSentryNoDamage), then its sub-state [0x890734]:
//   0 dormant 0x474a40 (woken: the player's "use"; variants 3 / 4 by their enemy group's marks (variant 4: also the
//     player within 0x400) -> the variant's routine [0x89075c]),
//   1 turn and march 0x474bf0 (rise, turn by 0x80 steps toward the player, march 0x20 ticks, sit back down),
//   2 the wake 0x474e80 (-> 4), 3 lobbed shots 0x4750c0 (until the player is within 0xfa of the drop point; -> 0),
//   4 the throw 0x474f80 (the drops of table 3 launched from 0xdc above, the point 0x12c0 ahead kept; -> 3), 5 six
//   lobbed shots 0x475290 (-> 0), 6 a march round 0x475370, 7 / 8 the stagger 0x4755c0 (back to the variant's routine,
//   which resumes its step), 9 dead 0x4756b0 (-> state 3).
// The hit check: the player's attacks flash it and every 4th one staggers it; a compared-height hit keeps its contact
// angle (and staggers it harder within / below); the damage taken (dead -> 9).
// The shadow (exe 0x5f20f0(actor, 0x30, 0x160, 0) unless state 2) and the OT draw are render output.
#include "content/saul_kada/st34_ruin_sentry.hpp"
#include "content/saul_kada/st34_ruin_sentry_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace st34_ruin_sentry_state;

namespace {

enum class SentryState : uint8_t { Bind = 0, Run = 1, Dying = 3 };
enum class SentrySub : uint8_t {
    Dormant = 0, TurnMarch = 1, Wake = 2, Lob = 3, Throw = 4, Volley = 5, MarchRound = 6, Stagger = 7, Knocked = 8,
    Dead = 9
};
enum class SentryAnim : uint8_t {
    Walk = 1, Rise = 2, Unfold = 3, Throw = 4, ThrowEnd = 5, Stagger = 6, Dead = 7, Turn = 8, Sit = 9, FoldedHit = 0xa,
    Dormant = 0xb, Fold = 0xc
};
constexpr uint8_t kSentryNoHit = 0x01;      // no hit entry
constexpr uint8_t kSentryNoDamage = 0x02;   // no hit check
constexpr uint8_t kSentryTwoBoxes = 0x10;   // a second sphere above the first
constexpr uint8_t kSentryArmBox = 0x20;     // its sphere 0x500 ahead of bone 0
constexpr uint8_t kSentryNoLockOn = 0x80;   // its entries never the lock-on target
// its flags while dormant / settled to shoot / dead (bits 4 / 8 too: not read by the class)
constexpr uint8_t kDormantFlags = 0x8f, kShootingFlags = 0x8c, kDeadFlags = 0x0e;
constexpr uint8_t kContactWallX = 0x01, kContactWallZ = 0x02, kContactNoGround = 0x08;
const DataTable<ScalarTable<uint8_t, 1>> sentryModelVariant{TableId::St34SentryModelVariant}; // exe data 0x890730
const DataTable<ScalarTable<uint8_t, 5>> wakeRoutines{TableId::St34RuinSentryWakeRoutines}; // the sub-state woken into, by variant
constexpr size_t kFoldedParts[4] = {2, 3, 4, 5};    // the parts hidden while dormant
constexpr uint16_t kStepSound = 0x125, kRiseSound = 0x126, kSitSound = 0x127, kShotSound = 0x161;
constexpr uint32_t kHitStaggers = kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong;
constexpr uint32_t kHitCompared = kHitLevelAbove | kHitLevelWithin | kHitLevelBelow;
constexpr uint32_t kHitComparedHard = kHitLevelWithin | kHitLevelBelow;
constexpr uint8_t kShotVariant = 1;
constexpr uint32_t kShotRise = 0x320000;  // 0x32 above it (16.16)
constexpr uint32_t kThrowRise = 0xdc0000; // 0xdc above it
constexpr uint32_t kThrowDrops = 3;
constexpr uint16_t kExplosionTint = 0x4214;

AnimatedActorStats sentryStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void solid(AnimatedActor& actor) { actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); } // exe 0x414050(1, 0, 0)
void enterSub(AnimatedActor& actor, SentrySub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void setAnim(AnimatedActor& actor, SentryAnim anim) { actor.anim = uint8_t(anim); }
bool footstepFrame(const AnimatedActor& actor) { return actor.animKey.frame == 2 || actor.animKey.frame == 0xc; }
void foldParts(AnimatedActor& actor, bool folded) {
    for (size_t part : kFoldedParts) {
        if (folded) actor.parts[part].flags |= kPartHidden;
        else actor.parts[part].flags &= uint8_t(~kPartHidden);
    }
}

// ---- the class's helpers -------------------------------------------------------------------------------------------
// exe 0x475840: the hit check; -1 dead (-> 9)
int hitCheck(AnimatedActor& actor, SentryFields& sentry) {
    const uint32_t result = sentry.hitBox.result, damage = result & kHitDamageMask;
    if (damage == 0) return 0;
    uint32_t taken = 0;
    if (result & kHitStaggers) {
        actor.tint = kTintFlash;
        const int8_t count = int8_t(uint8_t(sentry.staggerCount) - 1u);
        sentry.staggerCount = count;
        if (count <= 0) {
            sentry.staggerCount = 4;
            enterSub(actor, SentrySub::Stagger);
        }
        taken = damage;
    }
    if (result & kHitCompared) {
        sentry.counter = uint16_t(sentry.hitBox.contact.x);
        if (result & kHitComparedHard) {
            enterSub(actor, SentrySub::Knocked);
            actor.tint = kTintFlash;
        }
        taken = damage;
    }
    if (hitPointsAdd(actor, -int(taken)) != 0) { // exe 0x5e52f0
        enterSub(actor, SentrySub::Dead);
        return -1;
    }
    return 0;
}
// exe 0x4758f0: the walls (pushed out and turned off them; back on a failure) or its territory, the movers' queue, the
// ground followed; true when the ground fell away
bool move(const PlayerEnv& env, AnimatedActor& actor, SentryFields& sentry) {
    sentry.contact = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {        // exe 0x40f570
        const uint32_t result = actorPushOut(env, actor); // exe 0x410960
        reflectRotationOffWalls(actor.rotation, result);  // exe 0x410740
        if (int32_t(result) < 0) actor.pos = actor.prevPos;
        if (result & 0x300) sentry.contact |= kContactWallX;
        if (result & 0xc00) sentry.contact |= kContactWallZ;
    } else if (const int rect = territoryViolated(actor.pos)) { // exe 0x5e6ab0
        const uint32_t result = territoryPushBack(actor, rect);  // exe 0x5e6b40
        if (result & 0x300) sentry.contact |= kContactWallX;
        if (result & 0xc00) sentry.contact |= kContactWallZ;
    }
    actorCollisionSubmitMover(actor);            // exe 0x4140b0
    const int ground = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne != 0 || actor.onPlatform != 0) return false;
    if (ground - actor.pos.y.whole > 0x10) {
        sentry.contact |= kContactNoGround;
        return true;
    }
    actor.pos.y.whole = int16_t(ground);
    return false;
}
// exe 0x4759d0(actor, rise): a lobbed shot (variant 1) `rise` above it, aimed at the player (its yaw kept within
// 0x400 of its facing), its forward speed his planar distance / 16
void lobShot(const AnimatedActor& actor, uint32_t rise) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    const AnimatedActorStats stats = sentryStats(actor);
    int angle = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
    if (d < -0x400) angle += -0x400 - d;
    else if (d > 0x400) angle += 0x400 - d;
    if (!slot) return;
    ProjectileActor& shot = projectileActorAt(slot);
    LobbedShotParams params(projectileActorAt(slot));
    shot.flags = kActorLive;
    shot.type = kLobbedShotStageType;
    shot.variant = kShotVariant;
    shot.loopFlags |= kActorSweepGroup2;
    shot.pos.x = actor.pos.x;
    shot.pos.y = int32_t(uint32_t(actor.pos.y) - rise);
    shot.pos.z = actor.pos.z;
    shot.rotation.x = 0;
    shot.rotation.y = int16_t(angle);
    shot.rotation.z = 0;
    shot.velocity.x = 0;
    shot.velocity.y = int16_t(0xfe80);
    const int32_t reach = int32_t(uint32_t(planarDistance(player().pos, actor.pos)) << 4);
    shot.acceleration.y = 0x30;
    shot.velocity.z = int16_t(uint16_t(uint32_t(-((reach + ((reach >> 31) & 0xf)) >> 4))));
    params.size = 0x20;
    params.damage = int16_t(stats.shotAttack);
    params.launchSound = 0xad;
    sound(kShotSound, actor);
}
// the player's planar distance to the drop point (its integer x / z; the exe's stack point)
int dropPointDistance(const SentryFields& sentry) {
    const int32_t point[3] = {int32_t(uint32_t(uint16_t(sentry.dropX)) << 16), 0,
                              int32_t(uint32_t(uint16_t(sentry.dropZ)) << 16)};
    return planarDistance(player().pos, point);
}

// ---- the sub-states [0x890734] -------------------------------------------------------------------------------------
// exe 0x474a40: dormant (folded, its "use" asked of the player when he faces it)
void dormant(AnimatedActor& actor, SentryFields& sentry) {
    sentry.resume = 0;
    sentry.resumeStep = 0;
    if (actor.step == 0) {
        actor.step = 1;
        setAnim(actor, SentryAnim::Dormant);
        sentry.frozen = 1;
        foldParts(actor, true);
        sentry.flags = kDormantFlags;
    }
    actor.interaction =
        uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos))) +
                 0x180) > 0x300u
            ? 0
            : kInteractTalk;
    bool wake = false;
    const uint8_t variant = actor.variant;
    if (variant == 3) {
        if (enemyGroupMarks(sentry.group, EnemyGroupMarks::LastTick) != 0) wake = true;
        else if (player().talksTo(actor)) enemyGroupMark(sentry.group, 1);
    } else if (variant == 4) {
        if (enemyGroupMarks(sentry.group, EnemyGroupMarks::LastTick) != 0) wake = true;
        else if (uint32_t(distance3(actor.pos, player().pos)) < 0x400) enemyGroupMark(sentry.group, 1);
    } else {
        wake = player().talksTo(actor);
    }
    if (wake) {
        actor.restartStep();
        actor.subState = wakeRoutines[variant];
        if (variant != 3) {
            foldParts(actor, false);
            solid(actor);
            return;
        }
    }
    solid(actor);
}
// the turn of exe 0x474bf0 (0x474caa): turn steps of 0x80 toward the player (right: step 2, left: 3)
void startTurn(const PlayerEnv& env, AnimatedActor& actor, SentryFields& sentry) {
    actor.step = 2;
    setAnim(actor, SentryAnim::Turn);
    uint32_t off = (uint32_t(angleOf(actor.pos, player().pos)) - uint16_t(actor.rotation.y)) & 0xfff;
    if (off >= 0x800) {
        ++actor.step;
        off = 0xfff - off;
    }
    sentry.marchTimer = 0x20;
    sentry.counter = uint16_t(off >> 7);
    sentry.resumeStep = 2;
    move(env, actor, sentry);
}
// exe 0x474bf0: rise, turn, march, sit
void turnMarch(const PlayerEnv& env, AnimatedActor& actor, SentryFields& sentry) {
    if (sentry.resume != 0) {
        sentry.resume = 0;
        const uint8_t step = sentry.resumeStep;
        sentry.flags = 0;
        switch (step) {
        case 1:
        case 2: startTurn(env, actor, sentry); return;
        case 3:
            actor.step = 4;
            setAnim(actor, SentryAnim::Walk);
            sentry.flags |= kSentryTwoBoxes;
            break;
        case 4:
            actor.step = 5;
            setAnim(actor, SentryAnim::Sit);
            sound(kSitSound, actor);
            break;
        default: break;
        }
    }
    switch (actor.step) {
    case 0:
        actor.step = 1;
        setAnim(actor, SentryAnim::Rise);
        sentry.resumeStep = 1;
        sentry.frozen = 0;
        sound(kRiseSound, actor);
        sentry.flags = 0;
        [[fallthrough]];
    case 1:
        if (actor.animEnded()) {
            startTurn(env, actor, sentry);
            return;
        }
        break;
    case 2:
    case 3:
        actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + (actor.step == 2 ? 0x80u : 0xff80u)) & 0xfffu);
        sentry.counter -= 1u;
        if (sentry.counter != 0) break;
        actor.step = 4;
        setAnim(actor, SentryAnim::Walk);
        sentry.resumeStep = 3;
        sentry.flags |= kSentryTwoBoxes;
        move(env, actor, sentry);
        return;
    case 4:
        turnYawTo(actor.rotation.y, uint32_t(angleOf(actor.pos, player().pos)), 0x33); // exe 0x5e3df0
        integrateVelocity(actor, 0, 0, -0x200);                                          // exe 0x5e3690
        if (footstepFrame(actor)) sound(kStepSound, actor);
        sentry.marchTimer -= 1u;
        if (sentry.marchTimer != 0) break;
        actor.step = 5;
        setAnim(actor, SentryAnim::Sit);
        sentry.resumeStep = 4;
        sound(kSitSound, actor);
        move(env, actor, sentry);
        return;
    case 5:
        if (actor.animEnded()) enterSub(actor, SentrySub::Dormant);
        break;
    default: break;
    }
    move(env, actor, sentry);
}
// exe 0x474e80: the wake (-> the throw)
void wake(AnimatedActor& actor, SentryFields& sentry) {
    switch (actor.step) {
    case 0:
        setAnim(actor, SentryAnim::Rise);
        actor.step = 1;
        sentry.frozen = 0;
        sound(kRiseSound, actor);
        sentry.flags = 0;
        return;
    case 1:
        if (actor.animEnded()) {
            actor.step = 2;
            sentry.timer = 1;
        }
        [[fallthrough]];
    case 2:
        sentry.timer -= 1u;
        if (sentry.timer != 0) return;
        ++actor.step;
        setAnim(actor, SentryAnim::Unfold);
        sentry.timer = 3;
        return;
    case 3:
        sentry.timer -= 1u;
        if (sentry.timer == 0) {
            ++actor.step;
            sentry.flags |= kSentryArmBox;
        }
        [[fallthrough]];
    case 4:
        if (actor.animEnded()) {
            ++actor.step;
            sentry.flags &= uint8_t(~kSentryArmBox);
            enterSub(actor, SentrySub::Throw);
        }
        return;
    default: return;
    }
}
// exe 0x4750c0: lobbed shots until the player is within 0xfa of the drop point
void lob(AnimatedActor& actor, SentryFields& sentry) {
    switch (actor.step) {
    case 0:
        actor.step = 1;
        actor.prevAnim = 0xff;
        setAnim(actor, SentryAnim::Sit);
        sound(kSitSound, actor);
        sentry.flags = kShootingFlags;
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.step = 2;
        setAnim(actor, SentryAnim::Dormant);
        sentry.frozen = 1;
        sentry.flags |= kSentryNoHit;
        solid(actor);
        return;
    case 2:
        if (dropPointDistance(sentry) < 0xfa) {
            setAnim(actor, SentryAnim::Dormant);
            actor.step = 3;
            sentry.frozen = 0;
            sentry.flags = 0;
            return;
        }
        solid(actor);
        return;
    case 3: actor.step = 4; return;
    case 4:
        if (!actor.animEnded()) return;
        actor.step = 5;
        sentry.timer = 0x19;
        lobShot(actor, kShotRise);
        return;
    case 5:
        sentry.timer -= 1u;
        if (sentry.timer != 0) return;
        if (dropPointDistance(sentry) < 0xfa) {
            --actor.step;
            return;
        }
        setAnim(actor, SentryAnim::Fold);
        actor.step = 6;
        return;
    case 6: actor.step = 7; return;
    case 7:
        if (actor.animEnded()) enterSub(actor, SentrySub::Dormant);
        return;
    default: return;
    }
}
// exe 0x474f80: the throw of its drops (-> the lobbed shots)
void throwDrops(AnimatedActor& actor, SentryFields& sentry) {
    switch (actor.step) {
    case 0:
        setAnim(actor, SentryAnim::Throw);
        actor.step = 1;
        return;
    case 1: {
        if (!actor.animEnded()) return;
        actor.step = 2;
        // the drop point: 0x12c0 ahead (exe 0x5e36f0 on the exe's stack copy of its position)
        int32_t point[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
        int32_t d[3];
        rotateByYaw(actor.rotation.y, 0, 0, -0x12c0, d);
        for (int k = 0; k < 3; ++k) point[k] = int32_t(uint32_t(point[k]) + uint32_t(d[k]));
        sentry.dropX = int16_t(uint32_t(point[0]) >> 16);
        sentry.dropZ = int16_t(uint32_t(point[2]) >> 16);
        sentry.timer = 0xf;
        const int32_t from[3] = {actor.pos.x, int32_t(uint32_t(actor.pos.y) - kThrowRise), actor.pos.z};
        const uint32_t velocityXY = uint32_t(0xfe00) << 16; // x 0, y -0x200
        const uint32_t velocityZSpare = 0xff40u | uint32_t(uint16_t(uint16_t(actor.rotation.y) | kPickupAimed)) << 16;
        spawnLaunchedDrops(from, velocityXY, velocityZSpare, kThrowDrops); // exe 0x5f1360
        return;
    }
    case 2:
        sentry.timer -= 1u;
        if (sentry.timer != 0) return;
        setAnim(actor, SentryAnim::ThrowEnd);
        actor.step = 3;
        return;
    case 3:
        if (actor.animEnded()) enterSub(actor, SentrySub::Lob);
        return;
    default: return;
    }
}
// exe 0x475290: six lobbed shots
void volley(AnimatedActor& actor, SentryFields& sentry) {
    switch (actor.step) {
    case 0:
        sentry.flags = 0;
        setAnim(actor, SentryAnim::Dormant);
        actor.step = 1;
        sentry.frozen = 0;
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.step = 2;
        sentry.timer = 0x19;
        sentry.counter = 6;
        return;
    case 2: {
        sentry.timer -= 1u;
        if (sentry.timer != 0) return;
        const uint16_t left = sentry.counter;
        sentry.counter = uint16_t(left - 1u);
        if (left == 0) {
            setAnim(actor, SentryAnim::Fold);
            actor.step = 3;
            return;
        }
        sentry.timer = 0xf;
        lobShot(actor, kShotRise);
        return;
    }
    case 3:
        if (actor.animEnded()) enterSub(actor, SentrySub::Dormant);
        return;
    default: return;
    }
}
// the march of exe 0x475370 (0x475483 / 0x4754fd): its footsteps, forward 0x200
void marchStep(AnimatedActor& actor) {
    if (footstepFrame(actor)) sound(kStepSound, actor);
    integrateVelocity(actor, 0, 0, -0x200); // exe 0x5e3690
}
void startMarch(AnimatedActor& actor, SentryFields& sentry) { // exe 0x475422
    actor.step = 2;
    setAnim(actor, SentryAnim::Walk);
    sentry.marchTimer = 0x40;
    sentry.resumeStep = 5;
    sentry.flags |= kSentryTwoBoxes;
}
// exe 0x475370: rise, march 0x40, turn round, march 0x40, turn round, sit
void marchRound(AnimatedActor& actor, SentryFields& sentry) {
    if (sentry.resume != 0) {
        sentry.resume = 0;
        const uint8_t step = sentry.resumeStep;
        sentry.flags = 0;
        switch (step) {
        case 1: startMarch(actor, sentry); return;
        case 5:
            actor.step = 2;
            setAnim(actor, SentryAnim::Walk);
            sentry.flags |= kSentryTwoBoxes;
            break;
        case 6:
            actor.step = 3;
            setAnim(actor, SentryAnim::Turn);
            break;
        case 7:
            actor.step = 4;
            setAnim(actor, SentryAnim::Turn);
            break;
        default: break;
        }
    }
    switch (actor.step) {
    case 0:
        actor.step = 1;
        setAnim(actor, SentryAnim::Rise);
        sentry.resumeStep = 1;
        sentry.frozen = 0;
        sound(kRiseSound, actor);
        sentry.flags = kSentryTwoBoxes;
        [[fallthrough]];
    case 1:
        if (actor.animEnded()) startMarch(actor, sentry);
        return;
    case 2:
        sentry.marchTimer -= 1u;
        if (sentry.marchTimer == 0) {
            actor.step = 3;
            setAnim(actor, SentryAnim::Turn);
            sentry.counter = uint16_t(uint16_t(actor.rotation.y) + 0x800u);
            sentry.resumeStep = 6;
            sentry.marchTimer = 0x40;
        }
        marchStep(actor);
        return;
    case 3:
        if (actor.stepCounter == 0) {
            if (turnYawTo(actor.rotation.y, uint32_t(int32_t(int16_t(sentry.counter))), 0x33) != 0) return; // 0x5e3df0
            setAnim(actor, SentryAnim::Walk);
            ++actor.stepCounter;
            return;
        }
        sentry.marchTimer -= 1u;
        if (sentry.marchTimer == 0) {
            actor.enterStep(4); // the step and its counter as one word
            setAnim(actor, SentryAnim::Turn);
            sentry.counter = uint16_t(uint16_t(actor.rotation.y) + 0x800u);
            sentry.resumeStep = 7;
        }
        marchStep(actor);
        return;
    case 4:
        if (actor.stepCounter == 0) {
            if (turnYawTo(actor.rotation.y, uint32_t(int32_t(int16_t(sentry.counter))), 0x33) != 0) return; // 0x5e3df0
            setAnim(actor, SentryAnim::Sit);
            ++actor.stepCounter;
            sound(kSitSound, actor);
            return;
        }
        if (actor.animEnded()) enterSub(actor, SentrySub::Dormant);
        return;
    default: return;
    }
}
// exe 0x4755c0 (sub-states 7 / 8): the stagger, then the variant's routine (resumed)
void stagger(AnimatedActor& actor, SentryFields& sentry) {
    switch (actor.step) {
    case 0:
        sentry.flags = kSentryNoDamage;
        ++actor.step;
        setAnim(actor, actor.variant != 3 ? SentryAnim::Stagger : SentryAnim::FoldedHit);
        sentry.frozen = 0;
        sentry.timer = 0xf;
        return;
    case 1:
        sentry.timer -= 1u;
        if (sentry.timer != 0) return;
        sentry.resume = 1;
        switch (actor.variant) {
        case 1: enterSub(actor, SentrySub::TurnMarch); return;
        case 2: enterSub(actor, SentrySub::Lob); return;
        case 3:
            setAnim(actor, SentryAnim::Fold);
            ++actor.step;
            return;
        case 4: enterSub(actor, SentrySub::MarchRound); return;
        default: return;
        }
    case 2:
        if (actor.animEnded()) enterSub(actor, SentrySub::Dormant);
        return;
    default: return;
    }
}
// exe 0x4756b0: dead -> the dying state after 0x1e ticks
void dead(AnimatedActor& actor, SentryFields& sentry) {
    if (actor.step == 0) {
        sentry.flags = kDeadFlags;
        ++actor.step;
        sentry.timer = 0x1e;
        setAnim(actor, actor.variant == 3 ? SentryAnim::FoldedHit : SentryAnim::Dead);
    }
    sentry.timer -= 1u;
    if (sentry.timer != 0) return;
    actor.step = 0;
    ++actor.subState;
    actor.enterState(uint8_t(SentryState::Dying));
}

// ---- the states [0x89071c] -----------------------------------------------------------------------------------------
// exe 0x474920: the model, the hit points of its row, its boxes, talkable; its enemy group (variants 3 / 4)
void bind(AnimatedActor& actor, SentryFields& sentry) {
    if (!bindActorModel(actor, sentryModelVariant[0])) { // exe 0x5e0bc0
        freeActor(actor);                             // exe 0x5e1490
        return;
    }
    setAnim(actor, SentryAnim::Dormant);
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? sentryStats(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St34RuinSentryBox);
    actor.interactionBox.pointAt(TableId::St34RuinSentryTalkBox);
    actor.interaction = kInteractTalk;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    sentry.staggerCount = 4;
    sentry.marchTimer = 0;
    sentry.resumeStep = 1;
    sentry.frozen = 1;
    sentry.hitBox.previousX = actor.pos.x.whole;
    sentry.hitBox.previousZ = actor.pos.z.whole;
    sentry.hitBox.result = 0;
    const uint8_t variant = actor.variant;
    if (variant >= 3 && variant <= 4) sentry.group = uint8_t(enemyGroupFindFor(actor, variant)); // exe 0x5e6cd0
    actor.enterState(uint8_t(SentryState::Run));
}
// exe 0x474a00
void run(const PlayerEnv& env, AnimatedActor& actor, SentryFields& sentry) {
    actor.tint = kTintNeutral;
    if (!(sentry.flags & kSentryNoDamage)) hitCheck(actor, sentry); // exe 0x475840
    switch (SentrySub(actor.subState)) {                        // [0x890734]
    case SentrySub::Dormant: dormant(actor, sentry); return;
    case SentrySub::TurnMarch: turnMarch(env, actor, sentry); return;
    case SentrySub::Wake: wake(actor, sentry); return;
    case SentrySub::Lob: lob(actor, sentry); return;
    case SentrySub::Throw: throwDrops(actor, sentry); return;
    case SentrySub::Volley: volley(actor, sentry); return;
    case SentrySub::MarchRound: marchRound(actor, sentry); return;
    case SentrySub::Stagger:
    case SentrySub::Knocked: stagger(actor, sentry); return;
    case SentrySub::Dead: dead(actor, sentry); return;
    default: notPortedAt(kStoryPort, "0x474a00 sub-state (table overrun)", actor.subState);
    }
}
// exe 0x475710 (state 3): an explosion, blinking, its drops, freed
void dying(const PlayerEnv& env, AnimatedActor& actor, SentryFields& sentry) {
    actor.tint = kTintFlash;
    if (actor.step == 0) {
        actor.step = 1;
        sentry.timer = 0x14;
        if (actor.airborne == 0) {
            actor.velocity.x = 0;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
        } else {
            sentry.timer = 8;
        }
        actor.acceleration.y = 0x40;
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x60u);
            explosion.pos.z.whole = actor.pos.z.whole;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0x30;
            params.flashes = sentry.timer;
        }
    }
    if (sentry.timerLow & 1) actor.tint = kExplosionTint;
    sentry.timer -= 1u;
    if (sentry.timer == 0) {
        spawnDrops(actor.pos, sentryStats(actor).dropTable); // exe 0x5f1270
        freeActor(actor);                                    // exe 0x5e1490
        return;
    }
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    if (actor.airborne != 0) actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    move(env, actor, sentry); // exe 0x4758f0
    if (actor.wasAirborne != 0 && actor.airborne == 0) {
        actor.velocity.z = 0;
        actor.velocity.x = 0;
    }
}
// its hit entries: a sphere at bone 0 (the arm's 0x500 ahead of it, or a second one above it)
void submitHits(AnimatedActor& actor, SentryFields& sentry) {
    const AnimatedActorStats stats = sentryStats(actor);
    const uint8_t flags = sentry.flags;
    const uint32_t lockOn = uint32_t(int32_t(int8_t(flags)) & 0x80) << 12; // kSentryNoLockOn -> kHitNoLockOn
    const ShortVec4* bone = bonePointOrNull(actor, 0);                        // exe 0x5e1810
    sentry.hitBox.result = 0;
    sentry.hitBox.previousX = actor.prevPos.x.whole;
    sentry.hitBox.previousZ = actor.prevPos.z.whole;
    if (flags & kSentryNoHit) return;
    if (!bone) notPorted(kStoryPort, "0x474710: exe 0x5e1810(actor, 0) returned null (dereferenced)");
    const ShortVec4& point = *bone;
    int16_t pos[3] = {point.x, point.y, point.z};
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight);
    if (flags & kSentryArmBox) {
        moveAlongYaw16(pos, actor.rotation.y, 0, 0, -0x500); // exe 0x5e3750
        hitListSubmit(pos, sentry.hitBox,
                      owner | hitGroup(kHitGroupBodies) | kHitContactMotion | (uint32_t(stats.bodyAttack) >> 2) |
                          lockOn,
                      uint32_t(stats.strongAttack) | kHitAttackSolid); // exe 0x5e4120
        return;
    }
    pos[1] = int16_t(uint16_t(pos[1]) - 0x28u);
    hitListSubmit(pos, sentry.hitBox,
                  owner | hitGroup(kHitGroupBodies) | kHitCompareHeights | (uint32_t(stats.hitSize) >> 2) | lockOn,
                  uint32_t(stats.shotAttack) | kHitAttackSolid); // exe 0x5e4120
    if (sentry.flags & kSentryTwoBoxes) {
        pos[1] = int16_t(uint16_t(uint16_t(pos[1]) + 0x78u));
        hitListSubmit(pos, sentry.hitBox,
                      owner | lockOn | hitGroup(kHitGroupBodies) | kHitCompareHeights | hitRadius(0x50),
                      uint32_t(stats.shotAttack) | kHitAttackSolid);
    }
}
void ruinSentry(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x474710
    SentryFields& sentry = classState<SentryFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (SentryState(actor.state)) { // [0x89071c]
    case SentryState::Bind: bind(actor, sentry); break;
    case SentryState::Run: run(env, actor, sentry); break;
    case SentryState::Dying: dying(env, actor, sentry); break;
    default: notPortedAt(kStoryPort, "0x474710 state (null / table overrun)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) {
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    } else if (sentry.frozen == 0) {
        animStepPose(actor); // exe 0x5e1e80
    } else {
        animDecodeCurrent(actor); // exe 0x5e1f30
        animPose(actor);          // exe 0x5e2d90
    }
    submitHits(actor, sentry);
    // unless state 2: the shadow (exe 0x5f20f0(actor, 0x30, 0x160, 0)) -- render output
}

} // namespace

void ruinSentryUpdate(const PlayerEnv& env, uint32_t actor) { ruinSentry(env, animatedActorAt(actor)); }

} // namespace od::port
