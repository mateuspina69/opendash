// The static classes of the Manda ruins (ST12 / ST13 / ST14; see manda_ruin_props.hpp). The state tables are the exe's
// ([0x8ce764] / [0x8cf2a4] / [0x8d0714]); an entry past the class's own states (the exe's tables run into the next
// class's tables) is a port gap.
//
// The platform 0x59b710 (ST12 / ST13): states 0 init 0x59b820, 1 the platform 0x59b9f0 (sub-states [0x8ce770]: 0
//   0x59ba20, 1 0x59ba70 the run), 2 the wheel 0x59bc10 (variant & 4; sub-states [0x8ce778]: 0 0x59bc30, 1 0x59bc80
//   the spin). A platform runs back and forth along its yaw (the run length = its cells * 0x200 - 0x85) and stops while
//   the player stands on it; its companion is a wheel (type 0x17 variant 4) kept at the platform that spins up while
//   the player is on board, throwing sparks; variant & 2: a burrower rides it (pool 0x20 type 0x22 sub 1; skipped once
//   its enemy id is defeated). Every tick: the hit entry (none with kPropNoHitEntry; kPropHurts: a strong hit of the
//   row's damage) and, but for the wheel, the stand-on solid.
// The gates 0x5aa580 (ST12): bound by their variant, then every tick [0x8cf2a4 + variant * 4]: 0 0x5aa600 (script 1:
//   ticks 0x40 / 0x49 shake, 0x46 starts the lift: y up by 8 to -0x380), 1 0x5aa6c0 (script 4 tick 5: x down by 0x12
//   to 0x200), 2 0x5aa730 (script 4 tick 5: x up by 0x12 to 0).
// The debris piece 0x5b1c10 (ST14): states 0 0x5b1c30 (thrown up -0x300, back -0x140, gravity 0x28), 1 0x5b1c80 (spins
//   +0x100 a tick, moves along the yaw 0x200 until x < 0x201), 2 freed.
//
// Left out (render output): the platform's shadow (exe 0x5f20f0).
#include "content/manda/manda_ruin_props.hpp"
#include "content/manda/manda_ruin_props_state.hpp"

#include "content/manda/manda_ruin_burrower.hpp"
#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace manda_ruin_props_state;

namespace {

// ==== the platform 0x59b710 ==========================================================================================
enum class PlatformState : uint8_t {
    Init = 0,     // exe 0x59b820
    Platform = 1, // exe 0x59b9f0
    Wheel = 2,    // exe 0x59bc10
};
enum class PlatformSub : uint8_t {
    Start = 0, // exe 0x59ba20 / 0x59bc30
    Run = 1,   // exe 0x59ba70 (the platform) / 0x59bc80 (the wheel's spin)
};

// +0xc8
constexpr uint8_t kPropNoHitEntry = 0x01;
constexpr uint8_t kPropHurts = 0x20; // its hit entry hurts (the player away from it and not on board)
constexpr uint8_t kPlatformHitFlags = 0x06, kWheelHitFlags = 0x07;
// +0xca
constexpr uint8_t kRiderPresent = 0x80;
constexpr uint8_t kRiderIndexMask = 0x7f;
// variant bits
constexpr uint8_t kVariantFast = 0x01;  // the speed 0xfe (else 0x7f)
constexpr uint8_t kVariantRider = 0x02; // a burrower rides it
constexpr uint8_t kVariantWheel = 0x04; // the spinning wheel

constexpr uint8_t kWheelType = 0x17, kWheelVariant = 4;
constexpr uint8_t kRiderVariant = 1;
constexpr int16_t kRiderLift = 0x40;     // the rider stands this far above the platform's point
constexpr int16_t kHitPointLift = 0x20;  // the hit entry's point above the platform's
constexpr uint32_t kRunCellLength = 0x200, kRunMargin = 0x85;
constexpr uint32_t kHurtDistance = 0x58; // the player at least this far: the platform's entry hurts
constexpr uint32_t kOnBoardMask = 0x1fffff; // the player's platform word compared with the platform's address
constexpr int16_t kWheelRestSpin = -0xb0;   // the wheel's spin at rest (and its slowest)
constexpr int16_t kWheelSpinUp = 8, kWheelSpinDown = 4;
constexpr int8_t kWheelSparkSteps = 0x20;
constexpr uint8_t kWheelSparkStride = 3;
constexpr int kWheelSparkRadius = 0x48;

const DataTable<CollisionBoxRecord> platformBox{TableId::MandaRuinPropsPlatformBox};
const DataTable<InteractionBoxRecord> platformLiftBox{TableId::MandaRuinPropsPlatformLiftBox};

StaticActorStats platformStats(const StaticActor& actor) { return staticStats(actor.type, 0); }

// exe 0x59b820: the model, the boxes, the run; the wheel (state 2) or the platform on the ground with its wheel and
// its rider (state 1)
void platformInit(const PlayerEnv& env, StaticActor& actor) {
    auto&& f = classState<PlatformFields>(actor);
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.runFlags |= kActorRunMarked;
    const uint16_t hp = hasStaticStats(actor.type) ? platformStats(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(platformBox.stateAddress());
    actor.interactionBox.pointAt(platformLiftBox.stateAddress());
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    f.runLength = int16_t(uint16_t(uint32_t(f.runCells) * kRunCellLength - kRunMargin));
    actor.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.hitBox.result = 0;
    f.rider = 0;
    f.speed = (actor.variant & kVariantFast) ? 0xfe : 0x7f;
    if (actor.variant & kVariantWheel) {
        actor.enterState(uint8_t(PlatformState::Wheel));
        actor.flags |= kActorDrawn;
        return;
    }
    actor.flags &= uint8_t(~kActorDrawn);
    actor.pos.y.whole = int16_t(actorGround(env, actor));      // exe 0x40f190
    const uint32_t wheelAddress = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!wheelAddress) {
        freeActor(actor);
        return;
    }
    StaticActor& wheel = staticActorAt(wheelAddress);
    wheel.flags = kActorLive;
    wheel.type = kWheelType;
    wheel.variant = kWheelVariant;
    // the rotation's two dwords from the platform's pitch and roll, sign-extended (the exe's dword stores)
    wheel.rotation.setWords(uint32_t(int32_t(actor.rotation.x)), uint32_t(int32_t(actor.rotation.z)));
    wheel.setParamsWord(0);
    wheel.pos = actor.pos;
    f.companion = int8_t(staticPool().index(wheel));
    actor.enterState(uint8_t(PlatformState::Platform));
    if (!(actor.variant & kVariantRider)) return;
    const int8_t id = f.riderId;
    if (id != 0 && enemyDefeated(uint32_t(int32_t(id)))) return; // exe 0x5e6420
    const uint32_t riderAddress = allocActor20();                 // exe 0x5e1160
    if (!riderAddress) return;
    AnimatedActor& rider = animatedActorAt(riderAddress);
    rider.flags = kActorLive;
    rider.type = kBurrowerType;
    rider.variant = kRiderVariant;
    rider.pos = actor.pos;
    rider.pos.y.whole = int16_t(rider.pos.y.whole - kRiderLift);
    BurrowerParams(rider).enemyId = f.riderId;
    f.rider = uint8_t(uint8_t(animatedPool().index(rider)) | kRiderPresent);
}
// exe 0x59beb0: the player stands on this platform
bool playerOnBoard(const StaticActor& actor) {
    const Player& p = player();
    return p.onPlatform != 0 && ((slotIdentityWord(p.platform) ^ slotIdentityWord(actor)) & kOnBoardMask) == 0;
}
// exe 0x59be10: the scale 1.0 on the three axes
void scaleOne(StaticActor& actor) {
    actor.scale.x = kModelScaleOne;
    actor.scale.y = kModelScaleOne;
    actor.scale.z = kModelScaleOne;
}
// exe 0x59be30: the walls (back to the previous position; a floor / ceiling: true) and the territories
bool platformCollide(const PlayerEnv& env, StaticActor& actor) {
    classState<PlatformFields>(actor).landed = 0;
    if (int32_t(actorTestMove(env, actor)) >= 0) { // exe 0x40f570
        const int rect = territoryViolated(actor.pos); // exe 0x5e6ab0
        if (rect == 0) return false;
        territoryPushBack(actor, rect); // exe 0x5e6b40
        return false;
    }
    const uint32_t r = actorPushOut(env, actor); // exe 0x410960
    if (int32_t(r) < 0) actor.pos = actor.prevPos;
    if (!(r & kSolidAboveOrBelow)) return false;
    actor.pos = actor.prevPos;
    return true;
}
// exe 0x59ba70: back and forth along the yaw, held while the player is on board (the wheel driven); the rider and the
// wheel kept at the platform
void platformRun(StaticActor& actor) {
    auto&& f = classState<PlatformFields>(actor);
    if (actor.step == 0) {
        f.hitFlags = kPlatformHitFlags;
        actor.velocity.z = int16_t(-int(f.speed));
        f.distanceLeft = f.runLength;
        actor.step = 1;
    }
    const bool onBoard = playerOnBoard(actor);
    StaticActor& wheel = staticPool()[size_t(int32_t(f.companion))];
    auto&& wheelFields = classState<PlatformFields>(wheel);
    int16_t speed;
    if (onBoard) {
        speed = 0;
        wheelFields.driven = 1;
    } else {
        if (f.distanceLeft < 0) {
            actor.velocity.z = int16_t(-actor.velocity.z);
            f.distanceLeft = f.runLength;
        }
        speed = actor.velocity.z;
        wheelFields.driven = 0;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, speed); // exe 0x5e36f0
    int dx = int(actor.pos.x.whole) - int(actor.prevPos.x.whole);
    if (dx < 0) dx = int(actor.prevPos.x.whole) - int(actor.pos.x.whole);
    int dz = int(actor.pos.z.whole) - int(actor.prevPos.z.whole);
    if (dz < 0) dz = int(actor.prevPos.z.whole) - int(actor.pos.z.whole);
    if (int16_t(dx) > int16_t(dz)) f.distanceLeft = int16_t(f.distanceLeft - int16_t(dx));
    else f.distanceLeft = int16_t(f.distanceLeft - int16_t(dz));
    if (uint32_t(distance3(actor.pos, player().pos)) >= kHurtDistance && !onBoard) f.hitFlags |= kPropHurts;
    else f.hitFlags &= uint8_t(~kPropHurts);
    if (f.rider & kRiderPresent) {
        AnimatedActor& rider = animatedPool()[f.rider & kRiderIndexMask];
        rider.pos = actor.pos;
        rider.pos.y.whole = int16_t(rider.pos.y.whole + BurrowerParams(rider).hopHeight - kRiderLift);
    }
    wheel.pos = actor.pos;
}
// exe 0x59bdd0(x, y, z): a wheel spark (the global hit spark, variant 5)
void wheelSpark(uint16_t x, uint16_t y, uint16_t z) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& spark = effectActorAt(address);
    spark.flags = kActorLive;
    spark.loopFlags |= kActorSweepGroup2;
    spark.pos.x = int16_t(x);
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.variant = kSparkVariantDust;
    spark.pos.y = int16_t(y);
    spark.pos.z = int16_t(z);
    classState<HitSparkFields>(spark).size = kHitSparkSize;
}
// exe 0x59bc80: the wheel spins up while driven (sparks around it), else slows back to its rest spin
void wheelSpin(StaticActor& actor) {
    auto&& f = classState<PlatformFields>(actor);
    if (actor.step == 0) {
        f.hitFlags = kWheelHitFlags;
        actor.spin.y = kWheelRestSpin;
        actor.step = 1;
    }
    if (f.driven == 0) {
        actor.spin.y = int16_t(actor.spin.y - kWheelSpinDown);
        f.sparkStep = 0;
        if (actor.spin.y < kWheelRestSpin) actor.spin.y = kWheelRestSpin;
    } else {
        actor.spin.y = int16_t(actor.spin.y + kWheelSpinUp);
        if (actor.spin.y > 0) {
            actor.spin.y = 0;
        } else if (actor.spin.y != 0 && f.sparkStep < kWheelSparkSteps) {
            const int n = f.sparkStep;
            int sine = sin64(uint32_t(n));
            const int radius = (n >> 1) + kWheelSparkRadius;
            const int along = (radius * sine) >> 12;
            const int across = (cos64(uint32_t(n)) * radius) >> 12;
            sine *= n;
            const int drop = sine < 0 ? -(sine >> 12) : (sine >> 12);
            const uint16_t x = uint16_t(actor.pos.x.whole), y = uint16_t(uint16_t(actor.pos.y.whole) - uint16_t(drop));
            const uint16_t z = uint16_t(actor.pos.z.whole);
            switch (n & 3) {
            case 0: wheelSpark(uint16_t(x + along), y, uint16_t(z + across)); break;
            case 1: wheelSpark(uint16_t(x - along), y, uint16_t(z - across)); break;
            case 2: wheelSpark(uint16_t(x + across), y, uint16_t(z - along)); break;
            default: wheelSpark(uint16_t(x - across), y, uint16_t(z + along)); break;
            }
            f.sparkStep = int8_t(uint8_t(f.sparkStep) + kWheelSparkStride);
        }
    }
    actor.rotation.y = int16_t(actor.rotation.y + actor.spin.y);
}
// exe 0x59ba20 / 0x59bc30 (with 0x59ba50 / 0x59bc60): the motion cleared, the scale 1.0, the hit flags, the run
void platformStart(StaticActor& actor, uint8_t hitFlags) {
    actor.velocity.setXY(0, 0);
    actor.acceleration.setXY(0, 0);
    scaleOne(actor);
    classState<PlatformFields>(actor).hitFlags = hitFlags;
    actor.subState = uint8_t(PlatformSub::Run);
    actor.restartStep();
}
// exe 0x59b710
void platformUpdate(const PlayerEnv& env, StaticActor& actor) {
    auto&& f = classState<PlatformFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    actor.wasOnPlatform = actor.onPlatform;
    switch (PlatformState(actor.state)) {
    case PlatformState::Init: platformInit(env, actor); break;
    case PlatformState::Platform:
        actor.tint = kTintNeutral;
        switch (PlatformSub(actor.subState)) {
        case PlatformSub::Start: platformStart(actor, kPlatformHitFlags); break;
        case PlatformSub::Run: platformRun(actor); break;
        default: notPortedAt(kStoryPort, "static 0x59b710 platform sub-state", actor.subState);
        }
        platformCollide(env, actor);
        break;
    case PlatformState::Wheel:
        actor.tint = kTintNeutral;
        switch (PlatformSub(actor.subState)) {
        case PlatformSub::Start: platformStart(actor, kWheelHitFlags); break;
        case PlatformSub::Run: wheelSpin(actor); break;
        default: notPortedAt(kStoryPort, "static 0x59b710 wheel sub-state", actor.subState);
        }
        break;
    default: notPortedAt(kStoryPort, "static 0x59b710 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const int16_t point[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - kHitPointLift), actor.pos.z.whole};
    const StaticActorStats stats = platformStats(actor);
    const uint8_t hitFlags = f.hitFlags;
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(hitFlags & kPropNoHitEntry)) {
        const uint32_t attack = (hitFlags & kPropHurts) ? (kHitAttackThrown | kHitAttackStrong | stats.damage) : 0u;
        hitListSubmit(point, f.hitBox, // exe 0x5e4120
                      hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x58), attack);
    }
    if (actor.variant & kVariantWheel) return;
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, uint32_t(stats.damage) | kHitAttackSolid);
}

// ==== the gates 0x5aa580 =============================================================================================
enum class GateState : uint8_t { Init = 0, Run = 1 };
enum class GateVariant : uint8_t {
    Lift = 0,     // exe 0x5aa600: script 1
    SlideIn = 1,  // exe 0x5aa6c0: script 4 tick 5
    SlideOut = 2, // exe 0x5aa730: script 4 tick 5
};
const DataTable<CollisionBoxRecord> gateBox{TableId::MandaRuinPropsGateBox};
constexpr uint8_t kLiftScript = 1, kSlideScript = 4;
constexpr int32_t kLiftShakeTick = 0x40, kLiftStartTick = 0x46, kLiftStopShakeTick = 0x49, kSlideTick = 5;
constexpr int16_t kLiftSpeed = 8, kLiftTop = -0x380;
constexpr int16_t kSlideSpeed = 0x12, kSlideInStop = 0x200;


// exe 0x5aa600 (variant 0): the lift of script 1 -- shakes at its ticks 0x40 / 0x49, rises from 0x46 to -0x380
void gateLift(StaticActor& actor) {
    if (scriptState().part<CastTimelineStep>().stepSub == kLiftScript) {
        switch (int32_t(scriptState().part<CastTimelineTick>().stepTick)) {
        case kLiftShakeTick:
            cameraShake(1, 0x1800, 0x280); // exe 0x40ea80
            padVibrate(0x501, 0x1080aff);  // exe 0x55cd10
            break;
        case kLiftStartTick: actor.velocity.y = kLiftSpeed; break;
        case kLiftStopShakeTick:
            cameraShake(1, 0x80, 0);
            padVibrate(0, 0x1000050);
            break;
        default: break;
        }
    }
    if (actor.velocity.y == 0) return;
    if (actor.pos.y.whole <= kLiftTop) {
        actor.pos.y.whole = kLiftTop;
        actor.velocity.y = 0;
        cameraShake(1, 0x1800, 0x280);
        padVibrate(0x501, 0x1080aff);
        return;
    }
    actor.pos.y.whole = int16_t(actor.pos.y.whole - actor.velocity.y);
}
// exe 0x5aa6c0 (variant 1): slides in (x down by 0x12 to 0x200) from script 4's tick 5, a shake at the stop
void gateSlideIn(StaticActor& actor) {
    if (scriptState().part<CastTimelineStep>().stepSub == kSlideScript && scriptState().part<CastTimelineTick>().stepTick == kSlideTick) actor.velocity.x = int16_t(-kSlideSpeed);
    if (actor.velocity.x == 0) return;
    if (actor.pos.x.whole <= kSlideInStop) {
        actor.pos.x.whole = kSlideInStop;
        actor.velocity.x = 0;
        cameraShake(1, 0xc00, 0x100);
        padVibrate(0x501, 0x1000ac0);
        return;
    }
    actor.pos.x.whole = int16_t(actor.pos.x.whole + actor.velocity.x);
}
// exe 0x5aa730 (variant 2): slides out (x up by 0x12 to 0) from script 4's tick 5
void gateSlideOut(StaticActor& actor) {
    if (scriptState().part<CastTimelineStep>().stepSub == kSlideScript && scriptState().part<CastTimelineTick>().stepTick == kSlideTick) actor.velocity.x = kSlideSpeed;
    if (actor.pos.x.whole < 0) {
        actor.pos.x.whole = int16_t(actor.velocity.x + actor.pos.x.whole);
        return;
    }
    actor.pos.x.whole = 0;
    actor.velocity.x = 0;
}
// exe 0x5aa580
void gateUpdate(StaticActor& actor) {
    if (GateState(actor.state) == GateState::Init) {
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.collisionBox.pointAt(gateBox.stateAddress());
        actor.airborne = 0;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.enterState(uint8_t(GateState::Run));
    }
    switch (GateVariant(actor.variant)) {
    case GateVariant::Lift: gateLift(actor); return;
    case GateVariant::SlideIn: gateSlideIn(actor); return;
    case GateVariant::SlideOut: gateSlideOut(actor); return;
    default: notPortedAt(kStoryPort, "static 0x5aa580 variant", actor.variant);
    }
}

// ==== the debris piece 0x5b1c10 ======================================================================================
enum class DebrisState : uint8_t {
    Throw = 0, // exe 0x5b1c30
    Fly = 1,   // exe 0x5b1c80
    Gone = 2,  // exe 0x5afdd0
};
constexpr int kDebrisYaw = 0x200;
constexpr uint32_t kDebrisSpin = 0x100;
constexpr int16_t kDebrisLanding = 0x201; // it flies until its x is below this

// exe 0x5b1c10
void debrisUpdate(StaticActor& actor) {
    switch (DebrisState(actor.state)) {
    case DebrisState::Throw:
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.velocity.y = -0x300;
        actor.velocity.x = 0;
        actor.acceleration.x = 0;
        actor.acceleration.z = 0;
        actor.velocity.z = -0x140;
        actor.acceleration.y = 0x28;
        ++actor.state;
        return;
    case DebrisState::Fly:
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + kDebrisSpin) & kAngleMask);
        moveAlongYaw(actor.pos, kDebrisYaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        if (actor.pos.x.whole >= kDebrisLanding) return;
        ++actor.state;
        return;
    case DebrisState::Gone: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "static 0x5b1c10 state", actor.state);
    }
}

} // namespace

void mandaPlatformUpdate(const PlayerEnv& env, uint32_t actor) { platformUpdate(env, staticActorAt(actor)); }
void mandaGateUpdate(const PlayerEnv&, uint32_t actor) { gateUpdate(staticActorAt(actor)); }
void mandaDebrisPieceUpdate(const PlayerEnv&, uint32_t actor) { debrisUpdate(staticActorAt(actor)); }

} // namespace od::port
