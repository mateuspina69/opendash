// The pool-0x20 actors of the Nino chapter's fields (ST16 / ST17 / ST46; content/nino/ch2_scripts records), one routine
// per exe routine. Names are hypotheses from the observed behaviour. An entry past a class's own states (the exe's
// tables run into the next class's tables / data) is a port gap.
//
// The hovering target 0x49d1d0 (ST17 type 0x30 sub 1): states [0x893924]: 0 0x49d380 bind (box 0x893918, the hit points
//   of its row), 1 0x49d430 (the hits: sound 0x8d / 0x8e, the e0 timers 0x25 / 0x20, the smoke stage at 1/2 and 1/4 of
//   its hit points; sub-states [0x893930]: 0 0x49d6e0 wait for a smoke stage change, 1 0x49d700 rock; a bob of +-0x20),
//   2 0x49d7c0 (falls, enemy id 0xd defeated, freed below 0x1580). Smoke 0x49d5c0 (static type 3 at 0x49d870). Its
//   tail: two capsules (0x8c and 0x2a) + the solid.
// The hovering drifter 0x49de80 (ST46 type 0x30 sub 0): states [0x8939f4]: 0 0x49def0 bind (box 0x8939e8, variant 1
//   scale 0xa0), 1 0x49dfc0 (variants [0x8939fc]: 0 0x49dfe0, 1 0x49e000, 2 0x49e020 (smoke 0x49e110 every 4 ticks)); the
//   animation stepped.
// The crashing carrier 0x4d2e80 (ST16 type 0x49 sub 1): states [0x895420]: 0 0x4d2f30 bind (box 0x8953e0, the hit points
//   of its row, its home, its group), 1 0x4d2ff0 (sub-states [0x895430]: 0 0x4d3010 drift until flag 0x690) + exe
//   0x4d2d70, 2 0x4d3070 crash (box 0x8953ec, a defeat counted; smoke static type 3 every 4 ticks; explodes past the
//   animation's frame 0x15 or on a floor / ceiling), 3 freed.
// The scripted walker 0x4ea500 (ST46 type 0x63 sub 0): states [0x8964ec]: 0 0x4ea590 bind (box 0x8964e0, 1 hit point,
//   animation 1), 1 0x4ea620.
//
// Exe anomaly kept: 0x4d31e0 places the smoke column's z at the actor's x + offset (it adds +0x12, not +0x1a).
// A port gap where the exe would read through a null pointer (bone points, the stat row) or index past a table.
#include "content/nino/ch2_field_cast.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/ch2_field_cast_state.hpp"
#include "engine/data_source.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace ch2_field_cast_state;

namespace {

constexpr const char* kActorsPort = "nino field cast port"; // the area of its port-gap errors

constexpr uint8_t kNoAnim = 0xff;
constexpr uint8_t kTimerType = 0x1b;
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;


// the head of these classes' updates: the previous animation, the tint, the previous pose and contact
void castHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
}
// exe 0x5e1c30(actor, anim, entry) on a change, else exe 0x5e1e80
void animTick(AnimatedActor& actor, int entry) {
    if (actor.anim != actor.prevAnim)
        animSetPose(actor, actor.anim, entry);
    else
        animStepPose(actor);
}
// exe 0x41dd40(c) (as content/nino/field_spawners spawnTimer): an e0 timer controller with the value c
void spawnTimer(uint16_t c) {
    const uint32_t e = allocActorE0();
    if (e == 0) return;
    ControllerActor& timer = controllerActorAt(e);
    auto&& params = StageTimerParams(controllerActorAt(e));
    timer.flags = kActorLive;
    timer.type = kTimerType;
    timer.variant = 0;
    params.value = c;
    params.limit = 0x7f;
    params.flag = -1;
}

// [0x893924]
enum class HoverTargetState : uint8_t { Bind = 0, Main = 1, Fall = 2 };
// [0x893930]
enum class HoverTargetSub : uint8_t { Wait = 0, Rock = 1 };
// the fall's sub-states
enum class HoverFallSub : uint8_t { Start = 0, Roll = 1, Drop = 2 };
// [0x8939f4]
enum class HoverDrifterState : uint8_t { Bind = 0, Drift = 1 };
enum class DrifterRockStep : uint8_t { Start = 0, Rock = 1, Sink = 2 };

const DataTable<CollisionBoxRecord> hoverTargetBox{TableId::Ch2FieldCastHoverTargetBox};
const DataTable<CollisionBoxRecord> hoverDrifterBox{TableId::Ch2FieldCastHoverDrifterBox};
constexpr uint32_t kIdHoverTarget = 0xd;
constexpr int kToolKitPenalty = 10000;
constexpr int16_t kDrifterScale = 0xa0;

// exe 0x49d870: a smoke (the stage's static type 3 variant 0) at a point
void smokeAt(int16_t x, int16_t y, int16_t z) {
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!e) return;
    StaticActor& smoke = staticActorAt(e);
    auto&& params = SmokeParams(actorHeaderAt(e));
    smoke.flags = kActorLive;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.type = kSmokeType;
    smoke.subtype = 0;
    smoke.variant = 0;
    smoke.pos.x.whole = x;
    smoke.pos.y.whole = y;
    smoke.pos.z.whole = z;
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 4;
}
// exe 0x49d5c0(actor, smoke stage): every 8 ticks the smoke of the stage (1: one plume, 2: three)
void smokeOfStage(const AnimatedActor& actor, int stage) {
    if (stage != 1 && stage != 2) return;
    if (gameState().frame & 7) return;
    const int16_t x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    smokeAt(x, int16_t(y - 0x180), z);
    if (stage == 1) return;
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0x200u)), 0x180, dx, dz); // exe 0x5e40c0
    smokeAt(int16_t(dx + x), int16_t(y - 0x180), int16_t(dz + z));
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0x600u)), 0x280, dx, dz);
    smokeAt(int16_t(dx + x), y, int16_t(dz + z));
}
// the bind of 0x49d380 / 0x49def0: model variant 0, animation 0, the hit points of its row, the box, the class block
bool bindHover(AnimatedActor& actor, const DataTable<CollisionBoxRecord>& box) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return false;
    }
    animSetPose(actor, 0, 0);
    actor.anim = 0;
    actor.prevAnim = kNoAnim;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.acceleration.y = -1;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(box.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    return true;
}
// the rocking of 0x49d700 (amplitude 0xc0, step 0x18) / 0x49e020 (0x100, 0x10): the roll swings with its speed
// (acceleration x) bounded by the amplitude (spin z); each zero crossing loses a quarter of it; true when it is within
// -0x40 .. 0x3f
bool rock(AnimatedActor& actor, int step) {
    int16_t& rollSpeed = actor.acceleration.x;
    int16_t lim = actor.spin.z;
    const uint16_t r = uint16_t(actor.rotation.z) & 0xfff;
    actor.rotation.z = int16_t(int(rollSpeed >> 4) + uint16_t(actor.rotation.z));
    if (r <= 0x7ff) rollSpeed = int16_t(rollSpeed - step);
    if (r >= 0x801) rollSpeed = int16_t(rollSpeed + step);
    if (int(lim) < int(rollSpeed)) rollSpeed = lim;
    if (int(rollSpeed) < -int(lim)) rollSpeed = int16_t(-int(lim));
    if ((uint16_t(actor.prevRotation.z) ^ uint16_t(actor.rotation.z)) & 0x800) {
        lim = int16_t(lim - (lim >> 2));
        actor.spin.z = lim;
        if (uint16_t(lim + 0x40) <= 0x7f) return true;
    }
    return false;
}
// exe 0x49d6e0: waits for a smoke stage change, then rocks
void waitSmoke(AnimatedActor& actor, const HoverFields& hover) {
    if (hover.smokeChanged == 0) return;
    actor.subState = uint8_t(HoverTargetSub::Rock);
    actor.enterStep(0);
}
// exe 0x49d700: rocks (0xc0), then waits
void rockOnce(AnimatedActor& actor) {
    if (actor.step == 0) {
        actor.enterStep(1);
        actor.spin.z = 0xc0;
        actor.acceleration.x = 0xc0;
    }
    if (rock(actor, 0x18)) {
        actor.subState = uint8_t(HoverTargetSub::Wait);
        actor.enterStep(0);
    }
}
// exe 0x49d7c0: the fall -- rolls over to 0x100 (id 0xd defeated), drops; freed below 0x1580
void hoverFall(AnimatedActor& actor) {
    const uint8_t s = actor.subState;
    if (s <= uint8_t(HoverFallSub::Drop)) {
        if (s <= uint8_t(HoverFallSub::Roll)) {
            if (s == uint8_t(HoverFallSub::Start)) {
                actor.spin.x = 0;
                actor.rotation.z = 0;
                actor.subState = uint8_t(HoverFallSub::Roll);
            }
            actor.spin.x += 2u;
            if (actor.spin.x >= 0x101) actor.spin.x = 0x100;
            actor.rotation.z = int16_t(actor.rotation.z + (actor.spin.x >> 4));
            if (actor.rotation.z >= 0x101) {
                actor.rotation.z = 0x100;
                actor.subState = uint8_t(HoverFallSub::Drop);
                enemyMarkDefeated(kIdHoverTarget); // exe 0x5e63c0
            }
        }
        actor.velocity.y += 4u;
        if (actor.velocity.y >= 0x201) actor.velocity.y = 0x200;
        actor.pos.y = int32_t(actor.pos.y + int32_t(actor.velocity.y) * 0x1000);
    }
    animStepPose(actor);
    smokeOfStage(actor, 2);
    if (actor.pos.y.whole <= 0x1580) return;
    freeActor(actor);
}
// exe 0x49d430: the hits (the flash, the alarm on the player's first hit; dead: the tool kit's balance on the player's
// kill, the fall), the smoke stages, the sub-state, the bob, the animation, the smoke
void hoverMain(AnimatedActor& actor, HoverFields& hover) {
    const uint32_t r = hover.hitBox.result;
    hover.smokeChanged = 0;
    if ((r & kHitDamageMask) && hover.hurtTicks == 0) {
        uint16_t snd = kHurtSound;
        actor.tint = kTintFlash;
        if (r & kHitAttackStrongPush) {
            snd = kStrongHurtSound;
            hover.hurtTicks = 0xf;
        }
        soundPlayAtPos(snd, actor.pos);
        const uint32_t byPlayer = r & kHitAttackByPlayer;
        if (byPlayer && hover.alarmSent == 0) {
            spawnTimer(0x25);
            hover.alarmSent = 1;
        }
        if (hitPointsAdd(actor, -int(r & kHitDamageMask)) != 0) {
            if (byPlayer) toolKitBalanceAdd(-kToolKitPenalty); // exe 0x5e5260(0xffffd8f0)
            actor.enterState(uint8_t(HoverTargetState::Fall));
            hoverFall(actor);
            return;
        }
        if (hover.smokeStage == 0) {
            if (actor.hitPoints <= (actor.maxHitPoints >> 1)) {
                hover.smokeStage = 1;
                hover.smokeChanged = 1;
            }
        } else if (hover.smokeStage == 1 && actor.hitPoints <= (actor.maxHitPoints >> 2)) {
            hover.smokeStage = 2;
            hover.smokeChanged = 1;
            if (!byPlayer) spawnTimer(0x20);
        }
    }
    if (hover.hurtTicks) hover.hurtTicks -= 1u;
    switch (HoverTargetSub(actor.subState)) { // [0x893930]
    case HoverTargetSub::Wait: waitSmoke(actor, hover); break;
    case HoverTargetSub::Rock: rockOnce(actor); break;
    default: notPortedAt(kActorsPort, "0x49d430 sub past [0x893930]", actor.subState);
    }
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    if (actor.velocity.y >= 0x21) {
        actor.velocity.y = 0x20;
        actor.acceleration.y = -1;
    }
    if (actor.velocity.y <= -0x21) {
        actor.velocity.y = -0x20;
        actor.acceleration.y = 1;
    }
    actor.pos.y = int32_t(actor.pos.y + int32_t(actor.velocity.y) * 0x1000);
    animStepPose(actor);
    smokeOfStage(actor, hover.smokeStage);
}
// exe 0x49d1d0
void hoverTarget(AnimatedActor& actor) {
    auto&& hover = classState<HoverFields>(actor);
    castHead(actor);
    switch (HoverTargetState(actor.state)) { // [0x893924]
    case HoverTargetState::Bind:
        if (bindHover(actor, hoverTargetBox)) actor.enterState(uint8_t(HoverTargetState::Main));
        break;
    case HoverTargetState::Main: hoverMain(actor, hover); break;
    case HoverTargetState::Fall: hoverFall(actor); break;
    default: notPortedAt(kActorsPort, "0x49d1d0 state past [0x893924]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    hover.hitBox.result = 0;
    hover.hitBox.previousX = actor.prevPos.x.whole;
    hover.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    const uint32_t shape = hitOwner(actor.slot) | hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitCapsule(1);
    constexpr uint32_t kBodyAttack = kHitAttackHarmless | kHitAttackSolid;
    int16_t dx, dz;
    angleVector(actor.rotation.y, 300, dx, dz); // exe 0x5e40c0
    const int16_t p0[3] = {int16_t(dx + x), y, int16_t(dz + z)};
    angleVector(actor.rotation.y, -0x40, dx, dz);
    const int16_t p1[3] = {int16_t(dx + x), y, int16_t(dz + z)};
    hitListSubmitCapsule(p0, 0x8c, p1, 0x8c, hover.hitBox, shape, kBodyAttack); // exe 0x5e4190
    angleVector(actor.rotation.y, -0x180, dx, dz);
    const int16_t q0[3] = {int16_t(dx + x), int16_t(y - 0x200), int16_t(dz + z)};
    const int16_t q1[3] = {int16_t(dx + x), int16_t(y - 0x300), int16_t(dz + z)};
    hitListSubmitCapsule(q0, 0x2a, q1, 0x2a, hover.hitBox, shape, kBodyAttack);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
}

// exe 0x49e110(point): a smoke (the stage's static type 3 variant 2) at the 16.16 point
void trailSmoke(const Fixed16Vec3& pos) {
    const uint32_t e = allocActor60(kActorStageClass);
    if (!e) return;
    StaticActor& smoke = staticActorAt(e);
    auto&& params = SmokeParams(actorHeaderAt(e));
    smoke.variant = 2;
    smoke.flags = kActorLive;
    smoke.type = kSmokeType;
    smoke.subtype = 0;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.pos.x = pos.x;
    smoke.pos.y = pos.y;
    params.driftX = 0;
    smoke.pos.z = pos.z;
    params.driftZ = 0;
    params.life = 4;
}
// exe 0x49e020: rocks (0x100), sinks forward, smoke every 4 ticks
void drifterSink(AnimatedActor& actor) {
    const uint8_t s = actor.step;
    if (s == uint8_t(DrifterRockStep::Start) || s == uint8_t(DrifterRockStep::Rock)) {
        if (s == uint8_t(DrifterRockStep::Start)) {
            actor.enterStep(uint8_t(DrifterRockStep::Rock));
            actor.spin.z = 0x100;
            actor.acceleration.x = 0x100;
        }
        if (rock(actor, 0x10)) actor.enterStep(uint8_t(DrifterRockStep::Sink));
    }
    integrateVelocity(actor, 0, 0x80, -0x200);
    if (gameState().frame & 3) return;
    trailSmoke(actor.pos);
}
// exe 0x49de80
void hoverDrifter(AnimatedActor& actor) {
    castHead(actor);
    switch (HoverDrifterState(actor.state)) { // [0x8939f4]
    case HoverDrifterState::Bind: // exe 0x49def0
        if (bindHover(actor, hoverDrifterBox)) {
            if (actor.variant == 1) {
                actor.scale.z = kDrifterScale;
                actor.scale.y = kDrifterScale;
                actor.scale.x = kDrifterScale;
            }
            actor.enterState(uint8_t(HoverDrifterState::Drift));
        }
        break;
    case HoverDrifterState::Drift: // exe 0x49dfc0: by the variant [0x8939fc]
        switch (actor.variant) {
        case 0: integrateVelocity(actor, 0, 0, -0x80); break; // exe 0x49dfe0
        case 1:                                              // exe 0x49e000
            integrateVelocity(actor, 0, 0, -0x40);
            actor.rotation.y += 2u;
            break;
        case 2: drifterSink(actor); break;
        default: notPortedAt(kActorsPort, "0x49dfc0 variant past [0x8939fc]", actor.variant);
        }
        break;
    default: notPortedAt(kActorsPort, "0x49de80 state past [0x8939f4]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animStepPose(actor);
}

constexpr uint8_t kCrashHitWall = 0x01, kCrashHitFloor = 0x02, kCrashOffRoom = 0x08;
constexpr uint8_t kCarryRiders = 0x01, kCarryBusy = 0x04;
// [0x895420]
enum class CrashCarrierState : uint8_t { Bind = 0, Drift = 1, Crash = 2, Gone = 3 };
enum class CrashSub : uint8_t { Start = 0, Fall = 1 };
const DataTable<CollisionBoxRecord> crashCarrierBox{TableId::Ch2FieldCastCrashCarrierBox};
const DataTable<CollisionBoxRecord> crashCarrierFallBox{TableId::Ch2FieldCastCrashCarrierFallBox};
constexpr uint32_t kFlagCarrierDown = 0x690;
constexpr uint8_t kColumnType = 0x26, kColumnVariant = 1; // the stage's smoke column static
constexpr uint8_t kAnimHurt = 7;

// exe 0x4d2d70 (as content/nino/field_enemies carrierCollide): the walls -> the contact bits, the push-out result kept
void crashCollide(const PlayerEnv& env, AnimatedActor& actor, CrashCarrierFields& carrier) {
    carrier.contact = 0;
    const uint32_t r = actorTestMove(env, actor);
    if (r & 0x20000000) carrier.contact |= kCrashOffRoom;
    if (int32_t(r) >= 0) return;
    const uint32_t p = actorPushOut(env, actor);
    actor.wallHit = p;
    if (int32_t(p) < 0) actor.pos = actor.prevPos;
    if (p & 0xf00) carrier.contact |= kCrashHitWall;
    if (p & 0x3000) carrier.contact |= kCrashHitFloor;
}
// exe 0x4d31e0(actor, dx, dz, scale): a smoke column (the stage's static type 0x26 variant 1) at (x + dx, y, x + dz) --
// the exe adds the x for z as well; its scale, no speed, the actor's yaw
void smokeColumn(const AnimatedActor& actor, int16_t dx, int16_t dz, uint16_t scale) {
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!e) return;
    StaticActor& column = staticActorAt(e);
    column.flags = kActorLive;
    column.type = kColumnType;
    column.subtype = 0;
    column.variant = kColumnVariant;
    column.pos.x.whole = int16_t(dx + actor.pos.x.whole);
    column.pos.y.whole = actor.pos.y.whole;
    column.velocity.z = 0;
    column.pos.z.whole = int16_t(dz + actor.pos.x.whole);
    column.velocity.y = int16_t(scale);
    column.rotation.y = actor.rotation.y;
}
// exe 0x4d2f30: the model, drawn, the hit points of its row, no motion, the box, the class block, its home and group
void crashBind(AnimatedActor& actor, CrashCarrierFields& carrier) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = 0;
    actor.prevAnim = kNoAnim;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(crashCarrierBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    carrier.homeX = actor.pos.x.whole;
    carrier.homeZ = actor.pos.z.whole;
    carrier.group = uint8_t(enemyGroupFindFor(actor, 0)); // exe 0x5e6cd0
    actor.enterStateBytes(uint8_t(CrashCarrierState::Drift));
    actor.clearStepBytes();
}
// exe 0x4d3010: drifts forward at -0x180 until flag 0x690, then the crash
void crashDrift(AnimatedActor& actor, CrashCarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.carry = kCarryRiders;
        actor.spin.y = 0x30;
        actor.velocity.y = 0;
        actor.acceleration.z = -0x18;
        actor.enterStep(1);
    }
    integrateVelocity(actor, 0, 0, -0x180);
    if (!storyFlag(kFlagCarrierDown)) return;
    actor.enterState(uint8_t(CrashCarrierState::Crash));
}
// exe 0x4d3070: the crash -- the fall box, a defeat counted; it spins and falls (smoke every 4 ticks) until the
// animation's frame 0x15 or a floor: a blast and 3 smoke columns, freed
void crash(const PlayerEnv& env, AnimatedActor& actor, CrashCarrierFields& carrier) {
    if (CrashSub(actor.subState) == CrashSub::Start) {
        carrier.carry = kCarryBusy;
        actor.prevAnim = kNoAnim;
        actor.anim = kAnimHurt;
        carrier.fallYaw = actor.rotation.y;
        actor.subState = uint8_t(CrashSub::Fall);
        actor.collisionBox.pointAt(crashCarrierFallBox.stateAddress());
        countDefeat(); // exe 0x5e53e0
    } else if (actor.animKey.frame > 0x15 || (carrier.contact & kCrashHitFloor)) {
        if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300: the blast
            StaticActor& blast = staticActorAt(e);
            auto&& params = BlastParams(actorHeaderAt(e));
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.variant = 0;
            blast.loopFlags |= kActorSweepGroup2;
            blast.pos.x = actor.pos.x;
            blast.pos.y = actor.pos.y;
            params.size = 0xc0;
            blast.pos.z = actor.pos.z;
            params.attack = 4;
        }
        smokeColumn(actor, 0, 0, 0x30);
        smokeColumn(actor, 0xa0, 0x80, 0x40);
        smokeColumn(actor, -0x80, 0x40, 0x30);
        freeActor(actor);
        return;
    }
    actor.velocity.y += 0x10u;
    actor.rotation.y += 0x18u;
    actor.rotation.z += 0x18u;
    moveAlongYaw(actor.pos, carrier.fallYaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    if (!(carrier.timer & 3)) {
        if (const uint32_t e = allocActor60(kActorStageClass)) { // exe 0x5e12f0: smoke
            StaticActor& smoke = staticActorAt(e);
            auto&& params = SmokeParams(actorHeaderAt(e));
            smoke.flags = kActorLive;
            smoke.type = kSmokeType;
            smoke.variant = 0;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.pos.x = actor.pos.x;
            smoke.pos.y = actor.pos.y;
            params.driftX = 0;
            params.driftZ = 0;
            smoke.pos.z = actor.pos.z;
            params.life = 4;
        }
    }
    carrier.timer += 1u;
    crashCollide(env, actor, carrier);
}
// exe 0x4d2e80
void crashCarrier(const PlayerEnv& env, AnimatedActor& actor) {
    auto&& carrier = classState<CrashCarrierFields>(actor);
    castHead(actor);
    switch (CrashCarrierState(actor.state)) { // [0x895420]
    case CrashCarrierState::Bind: crashBind(actor, carrier); break;
    case CrashCarrierState::Drift: // exe 0x4d2ff0: [0x895430]
        if (actor.subState != 0) notPortedAt(kActorsPort, "0x4d2ff0 sub past [0x895430]", actor.subState);
        crashDrift(actor, carrier);
        crashCollide(env, actor, carrier);
        break;
    case CrashCarrierState::Crash: crash(env, actor, carrier); break;
    case CrashCarrierState::Gone: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kActorsPort, "0x4d2e80 state past [0x895420]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animTick(actor, 0);
    // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow (render output)
}

// [0x8964ec]
enum class WalkerState : uint8_t { Bind = 0, Walk = 1 };
enum class WalkerSub : uint8_t { Start = 0, Walk = 1, Turn = 2, WalkOn = 3, Stopped = 4 };
const DataTable<CollisionBoxRecord> scriptedWalkerBox{TableId::Ch2FieldCastScriptedWalkerBox};

// exe 0x4ea620: walks 30 ticks, turns for 30, walks 45, stops
void walkerWalk(AnimatedActor& actor, WalkerFields& walker) {
    switch (WalkerSub(actor.subState)) {
    case WalkerSub::Start:
        walker.timer = 0x1e;
        actor.subState = uint8_t(WalkerSub::Walk);
        [[fallthrough]];
    case WalkerSub::Walk:
        integrateVelocity(actor, 0, 0, -0x30);
        walker.timer -= 1u;
        if (walker.timer != 0) return;
        walker.timer = 0x1e;
        actor.spin.y = 0x44;
        actor.subState = uint8_t(WalkerSub::Turn);
        return;
    case WalkerSub::Turn:
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y));
        integrateVelocity(actor, 0, 0, -0x30);
        walker.timer -= 1u;
        if (walker.timer != 0) return;
        walker.timer = 0x2d;
        actor.subState = uint8_t(WalkerSub::WalkOn);
        return;
    case WalkerSub::WalkOn:
        integrateVelocity(actor, 0, 0, -0x30);
        walker.timer -= 1u;
        if (walker.timer != 0) return;
        actor.subState = uint8_t(WalkerSub::Stopped);
        return;
    default: return;
    }
}
// exe 0x4ea590: the model, no motion, drawn, animation 1, one hit point, the box, the class block; then the walk
void walkerBind(AnimatedActor& actor, WalkerFields& walker) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    const uint8_t flags = actor.flags;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.flags = uint8_t(flags | kActorDrawn);
    actor.anim = 1;
    actor.prevAnim = kNoAnim;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(scriptedWalkerBox.stateAddress());
    actor.clearClassBlock();
    actor.enterStateBytes(uint8_t(WalkerState::Walk));
    actor.clearStepBytes();
    walkerWalk(actor, walker);
}
// exe 0x4ea500
void scriptedWalker(AnimatedActor& actor) {
    auto&& walker = classState<WalkerFields>(actor);
    castHead(actor);
    switch (WalkerState(actor.state)) { // [0x8964ec]
    case WalkerState::Bind: walkerBind(actor, walker); break;
    case WalkerState::Walk: walkerWalk(actor, walker); break;
    default: notPortedAt(kActorsPort, "0x4ea500 state past [0x8964ec]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animTick(actor, 0);
}

} // namespace

void hoverTargetUpdate(const PlayerEnv&, uint32_t actor) { hoverTarget(animatedActorAt(actor)); }
void hoverDrifterUpdate(const PlayerEnv&, uint32_t actor) { hoverDrifter(animatedActorAt(actor)); }
void carrierCrashUpdate(const PlayerEnv& env, uint32_t actor) { crashCarrier(env, animatedActorAt(actor)); }
void scriptedWalkerUpdate(const PlayerEnv&, uint32_t actor) { scriptedWalker(animatedActorAt(actor)); }

} // namespace od::port
