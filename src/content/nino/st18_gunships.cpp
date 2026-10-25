// The ST18 gunships (see st18_gunships.hpp), one routine per exe routine; names are hypotheses from the observed
// behaviour.
//
// The cutscene gunship 0x5a2cc0: states [0x8cebf0]: 0 0x5a2d20 init, 1 0x5a2dc0 (sub-states [0x8cec00]: 0 0x5a2e00
// start by the variant, 1 0x5a2eb0 hover, 2 0x5a2f20 turn to 0xe00 and pitch down / up, 3 0x5a3060 weave between 0xe00
// and 0x600 trailing smoke), 2 0x52ef00 (nothing), 3 0x5afdd0 (freed).
//
// The ship fight's gunships 0x59e630: states [0x8ce960]: 0 0x59e770 init (a variant != 0 starts at sub-state 2), 1
// 0x59e810 (sub-states [0x8ce970]: 0 0x59e8f0 / 1 0x59e930 circle and shoot, 2 0x59ec30 climb in from the edge, 3
// 0x59ed60 circle the square's centre dropping Birdbots (pool 0x20 type 0x2d sub 1, the free track ids of the stage
// block), 4 0x59f100 / 5 0x59f230 fly off), 2 0x59f380 shot down (the crash and the drops), 3 0x5afdd0 freed. The
// square's centre is the exe's stack point {0, (uninitialised), 0}: only its x / z are read.
#include "content/nino/st18_gunships.hpp"
#include "content/nino/st18_gunships_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st18_gunships_state;

namespace {

// ---- the cutscene gunship 0x5a2cc0 -------------------------------------------------------------------------------

// [0x8cebf0]
enum class CutsceneGunshipState : uint8_t { Init = 0, Main = 1, Idle = 2, Gone = 3 };
// [0x8cec00]
enum class CutsceneGunshipSub : uint8_t { Start = 0, Hover = 1, Pitch = 2, Weave = 3 };
// the pitch's phases (the step counter)
enum class PitchPhase : uint8_t { Start = 0, Down = 1, Up = 2, Level = 3 };
// the weave's steps
enum class WeaveStep : uint8_t { Start = 0, TowardNorth = 1, TowardSouth = 2 };

const DataTable<CollisionBoxRecord> cutsceneGunshipBox{TableId::St18GunshipsCutsceneGunshipBox};
constexpr int16_t kSpeedStep = -0x20;
constexpr int8_t kLevelPitch = 0x30;
constexpr uint32_t kPitchYaw = 0xe00, kWeaveYawA = 0xe00, kWeaveYawB = 0x600;
constexpr uint16_t kPitchSound = 0x1a7;
constexpr int32_t kPitchStepTick = 0x32; // the pitch moves the ship until the script's tick
constexpr int32_t kPitchFloor = int32_t(0xf4000000u);

// exe 0x5a3200(actor, angle, lim, acc): the pitch turned with its rate (spin x, step spin z) toward the angle (as
// steerRate on the yaw)
int pitchRate(StaticActor& actor, uint32_t angle, int lim, int16_t acc) {
    int16_t& step = actor.spin.z;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), angle);
    if (uint32_t(d * 0x10 + lim) <= uint32_t(lim * 2)) return 0;
    step = 0;
    if (d < 0) step = int16_t(-acc);
    if (d > 0) step = acc;
    const int16_t r = actor.spin.x;
    actor.rotation.x = int16_t(actor.rotation.x + (r >> 4));
    actor.spin.x = int16_t(r + step);
    if (actor.spin.x > lim) actor.spin.x = int16_t(lim);
    if (actor.spin.x < -lim) actor.spin.x = int16_t(-lim);
    return d;
}
// exe 0x5a32e0(actor, acc): the pitch rate back to 0 (1 when it got there)
int pitchDamp(StaticActor& actor, int16_t acc) {
    int16_t& step = actor.spin.z;
    if (actor.spin.x < 0) step = acc;
    if (actor.spin.x > 0) step = int16_t(-acc);
    actor.rotation.x = int16_t(actor.rotation.x + (actor.spin.x >> 4));
    const int16_t r = int16_t(actor.spin.x + step);
    actor.spin.x = r;
    if (r == step) {
        actor.spin.x = 0;
        return 1;
    }
    return 0;
}

// the flight byte, the level bits and the speed step of the moves' first step
void levelFlight(StaticActor& actor, GunshipFields& ship) {
    ship.flight = 0;
    ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
    actor.acceleration.z = kSpeedStep;
}

// exe 0x5a2eb0: hovers forward at -0x400
void cutsceneHover(StaticActor& actor, GunshipFields& ship) {
    if (actor.step == 0) {
        levelFlight(actor, ship);
        actor.enterStep(1);
    }
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x400) != 0) actor.velocity.z = -0x400; // 0x5e5510
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
}

// exe 0x5a2f20: turned to yaw 0xe00, pitched down then up (sound 0x1a7), level; until the script's tick 0x32 the pitch
// moves it vertically (not below y -0xc00)
void cutscenePitch(StaticActor& actor) {
    if (actor.step != 0 || steerRate(actor, kPitchYaw, 0x240, 0x18) == 0) { // exe 0x5a3170
        if (actor.step == 0) actor.step = 1;
        steerDamp(actor, 0x18); // exe 0x5a3290
    }
    switch (PitchPhase(actor.stepCounter)) {
    case PitchPhase::Start:
        actor.spin.x = -0x300;
        actor.rotation.x = -0x80;
        actor.stepCounter = uint8_t(PitchPhase::Down);
        actor.velocity.z = -0x180;
        [[fallthrough]];
    case PitchPhase::Down:
        if (pitchRate(actor, uint32_t(-0x200), 0x300, 0x80) != 0) break;
        actor.stepCounter += 1u;
        pitchDamp(actor, 0x80);
        break;
    case PitchPhase::Up:
        if (pitchRate(actor, 0x200, 0x300, 0x80) != 0) break;
        actor.stepCounter += 1u;
        pitchDamp(actor, 0x80);
        soundPlayFlatForced(kPitchSound); // exe 0x5d5260
        break;
    case PitchPhase::Level: pitchDamp(actor, 0x80); break;
    default: break;
    }
    if (scriptState().stepTick >= kPitchStepTick) return;
    const int16_t rot[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
    int32_t o[3];
    rotateVector(rot, 0, 0, actor.velocity.z, o); // exe 0x5e3970: only its y is used
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
    if (actor.pos.y > kPitchFloor) actor.pos.y = kPitchFloor;
}

// exe 0x5a3060: weaves between yaw 0xe00 and 0x600 every 0x28 ticks, smoke every other tick
void cutsceneWeave(StaticActor& actor, GunshipFields& ship) {
    switch (WeaveStep(actor.step)) {
    case WeaveStep::Start:
        actor.step = uint8_t(WeaveStep::TowardNorth);
        ship.timer = 0x14;
        [[fallthrough]];
    case WeaveStep::TowardNorth:
        steerRate(actor, kWeaveYawA, 0x180, 0x10);
        ship.timer -= 1u;
        if (ship.timer == 0) {
            ship.timer = 0x28;
            actor.step = uint8_t(WeaveStep::TowardSouth);
        }
        break;
    case WeaveStep::TowardSouth:
        steerRate(actor, kWeaveYawB, 0x180, 0x10);
        ship.timer -= 1u;
        if (ship.timer == 0) {
            ship.timer = 0x28;
            actor.step = uint8_t(WeaveStep::TowardNorth);
        }
        break;
    default: break;
    }
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x200) != 0) actor.velocity.z = -0x200;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (!(ship.timer & 1)) gunshipSmoke(actor);
}

// exe 0x5a2e00: the rates cleared, then by the variant: 0 the hover, 1 the pitch, 2 the weave
void cutsceneStart(StaticActor& actor, GunshipFields& ship) {
    actor.spin.x = 0;
    actor.spin.y = 0;
    ship.flight = 0;
    ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
    switch (actor.variant) {
    case 0:
        ship.cooldown = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x400;
        actor.acceleration.setXY(0, 0);
        actor.subState = uint8_t(CutsceneGunshipSub::Hover);
        actor.enterStep(0);
        cutsceneHover(actor, ship);
        return;
    case 1:
        ship.cooldown = 0x100;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x400;
        actor.acceleration.setXY(0, 0);
        actor.subState = uint8_t(CutsceneGunshipSub::Pitch);
        actor.enterStep(0);
        cutscenePitch(actor);
        return;
    case 2:
        ship.cooldown = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x300;
        actor.subState = uint8_t(CutsceneGunshipSub::Weave);
        actor.enterStep(0);
        cutsceneWeave(actor, ship);
        return;
    default: return;
    }
}

// exe 0x5a2d20: the model (variant 1), the hit points of the variant-1 row * 8, the box, airborne
void cutsceneInit(StaticActor& actor) {
    if (!bindActorModel(actor, 1)) { // exe 0x5e0bc0(a, &1)
        freeActor(actor);
        return;
    }
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 1).hitPoints << 3) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.collisionBox.pointAt(cutsceneGunshipBox.stateAddress());
    actor.airborne = 1;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(CutsceneGunshipState::Main));
}

// exe 0x5a2dc0: the sub-state, the sway, the roll toward the turn
void cutsceneMain(StaticActor& actor, GunshipFields& ship) {
    switch (CutsceneGunshipSub(actor.subState)) {
    case CutsceneGunshipSub::Start: cutsceneStart(actor, ship); break;
    case CutsceneGunshipSub::Hover: cutsceneHover(actor, ship); break;
    case CutsceneGunshipSub::Pitch: cutscenePitch(actor); break;
    case CutsceneGunshipSub::Weave: cutsceneWeave(actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x5a2cc0 sub-state", actor.subState);
    }
    if (ship.flight & kFlightSway) sway(actor); // exe 0x5a0600
    rollToTurn(actor);                          // exe 0x5a25a0
}

// exe 0x5a2cc0
void cutsceneGunship(StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CutsceneGunshipState(actor.state)) {
    case CutsceneGunshipState::Init: cutsceneInit(actor); break;
    case CutsceneGunshipState::Main: cutsceneMain(actor, ship); break;
    case CutsceneGunshipState::Idle: break; // exe 0x52ef00
    case CutsceneGunshipState::Gone: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "static 0x5a2cc0 state", actor.state);
    }
    if (actor.flags & kActorLive) ship.flight &= uint8_t(~kGuns);
}

// ---- the ship fight's gunships 0x59e630 ------------------------------------------------------------------------------

enum class FightOrders : int8_t { None = 0, ToSquare = 1, Now = 2 };

// [0x8ce960]
enum class FightGunshipState : uint8_t { Init = 0, Alive = 1, Crash = 2, Gone = 3 };
// [0x8ce970]
enum class FightGunshipSub : uint8_t { CruiseStart = 0, Cruise = 1, ClimbIn = 2, Circle = 3, FlyOff = 4, Leave = 5 };
enum class FightCruiseStep : uint8_t {
    Init = 0, PickHeading = 1, Cruise = 2, Burst = 3, RunStart = 4, Run = 5, PauseStart = 6, Pause = 7
};
enum class ClimbInStep : uint8_t { Init = 0, Climb = 1, Level = 2 };
enum class CircleStep : uint8_t { Init = 0, Approach = 1, Turn = 2, Orbit = 3, Hold = 4 };
enum class FlyOffStep : uint8_t { Init = 0, Out = 1, Climb = 2 };
enum class LeaveStep : uint8_t { Init = 0, Out = 1, TurnBack = 2 };

const DataTable<CollisionBoxRecord> fightGunshipBox{TableId::St18GunshipsFightGunshipBox};
const DataTable<ScalarTable<uint8_t, 16>> fightCruiseTicks{TableId::St18GunshipsFightCruiseTicks};
const DataTable<ScalarTable<int16_t, 8>> fightHeadings{TableId::St18GunshipsFightHeadings};
const DataTable<ScalarTable<uint8_t, 16>> fightRunTicks{TableId::St18GunshipsFightRunTicks};
const DataTable<ScalarTable<uint8_t, 16>> fightBurstTicks{TableId::St18GunshipsFightBurstTicks};
const DataTable<ScalarTable<uint8_t, 16>> dropCooldowns{TableId::St18GunshipsDropCooldowns};
constexpr int kHomeAbove = 0x4b0;
constexpr uint8_t kBirdbotType = 0x2d, kBirdbotSub = 1;
constexpr int kSquareRadius = 0x800;

// the square's centre (x / z 0)
constexpr int32_t kSquareCentre[3] = {0, 0, 0};
// the point 0x90 behind the ship along its yaw (exe 0x5e40c0)
void pointBehind(const StaticActor& actor, int32_t out[3]) {
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u))), 0x90, dx, dz);
    out[0] = int32_t(uint32_t(actor.pos.x) + (uint32_t(int32_t(dx)) << 16));
    out[1] = actor.pos.y;
    out[2] = int32_t(uint32_t(actor.pos.z) + (uint32_t(int32_t(dz)) << 16));
}
// the bearing from a 16.16 point to another
int bearing(const int32_t from[3], const int32_t to[3]) { return angleOf(from[0], from[2], to[0], to[2]); }
// the bearing from the square's centre to the ship
int bearingFromCentre(const StaticActor& actor) {
    return angleOf(kSquareCentre[0], kSquareCentre[2], actor.pos.x, actor.pos.z);
}

// exe 0x59f740: the room collision bits (no skip while entering, no side bit)
void fightCollide(const PlayerEnv& env, StaticActor& actor, GunshipFields& ship) {
    uint8_t b = 0;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t q[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    const uint32_t r = testMoveBlocked(env, p, q, actor.collisionBox, 2); // exe 0x40f5a0
    if (r & 0x20000000) b = kContactWater;
    if (int32_t(r) < 0) {
        b |= kContactWall;
        if (!(ship.behaviour & kBehaveNoPushOut) && (actorPushOutMask(env, actor, 2) & 0xf00))
            b |= kContactFloor; // exe 0x410980
    }
    ship.contact = b;
}

// exe 0x59f7a0(actor, variant, yaw, id): a Birdbot (pool 0x20 type 0x2d sub 1) 0x90 behind, 0x80 below, tracked as id
uint32_t dropBirdbot(const StaticActor& actor, uint8_t variant, uint16_t yaw, uint32_t id) {
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u))), 0x90, dx, dz);
    const uint32_t r = allocActor20(); // exe 0x5e1160
    if (!r) return 0;
    AnimatedActor& bot = animatedActorAt(r);
    bot.variant = variant;
    bot.flags = kActorLive;
    bot.type = kBirdbotType;
    bot.subtype = kBirdbotSub;
    bot.loopFlags |= kActorSweepGroup2;
    bot.params[0] = 0;
    bot.params[3] = uint8_t(id);
    bot.pos.x.whole = int16_t(actor.pos.x.whole + dx);
    bot.pos.y.whole = int16_t(actor.pos.y.whole + 0x80);
    bot.rotation.y = int16_t(yaw);
    bot.pos.z.whole = int16_t(actor.pos.z.whole + dz);
    enemyClearDefeated(id);                        // exe 0x5e63f0
    encounterTrackInScriptSlot(bot, uint16_t(id)); // exe 0x5e6500
    return r;
}

// exe 0x59f0a0(actor, block, centre): the radius correction (the turn margin) -- +-0x10 outside 0x800 +- 0x80 of the
// centre (the orbit's side flips it)
void orbitRadiusCorrection(const StaticActor& actor, FightGunshipFields& ship) {
    int b = ship.turnMargin;
    const int d = planarDistance(actor.pos, kSquareCentre) - kSquareRadius; // exe 0x5e3b00
    if (uint32_t(d) > 0x100) {
        if (d > 0x80) b = -0x10;
        if (d < -0x80) b = 0x10;
        if (ship.orbit < 0) b = -b;
    }
    ship.turnMargin = int8_t(b);
}

// exe 0x59e930: as the square gunship's fly (its tables 0x8ce988 / 0x8ce998 / 0x8ce9b8 / 0x8ce9a8); told to: to the
// square
void fightCruise(StaticActor& actor, FightGunshipFields& ship) {
    switch (FightCruiseStep(actor.step)) {
    case FightCruiseStep::Init:
        ship.flight = kFlightProbeWalls;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(uint8_t(FightCruiseStep::PickHeading));
        [[fallthrough]];
    case FightCruiseStep::PickHeading:
        ship.behaviour &= uint8_t(~kBehaveStraight);
        ship.timer = fightCruiseTicks[rngStep() & 0xf];
        actor.enterStep(uint8_t(FightCruiseStep::Cruise));
        ship.heading = fightHeadings[rngStep() & 7];
        [[fallthrough]];
    case FightCruiseStep::Cruise: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.enterStep(uint8_t(FightCruiseStep::RunStart));
        // the exe passes the player's struct (not its position): its +0 / +8 words
        const int dist = planarDistance(actor.pos, playerStructAsPoint());
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
        if (uint32_t(d + 0x200) < 0x400 && dist <= 0xbb8) {
            ship.timer = fightBurstTicks[rngStep() & 0xf];
            actor.enterStep(uint8_t(FightCruiseStep::Burst));
        }
        break;
    }
    case FightCruiseStep::Burst: {
        const uint8_t t = ship.timer;
        if (!(t & 3)) ship.flight |= (t & 7) ? kGunLeft : kGunRight;
        ship.timer = uint8_t(t - 1);
        if (uint8_t(t - 1) == 0) actor.enterStep(uint8_t(FightCruiseStep::RunStart));
        break;
    }
    case FightCruiseStep::RunStart:
        ship.timer = fightRunTicks[rngStep() & 0xf];
        actor.enterStep(uint8_t(FightCruiseStep::Run));
        ship.behaviour |= kBehaveStraight;
        [[fallthrough]];
    case FightCruiseStep::Run: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0 || !(ship.behaviour & kBehaveStraight)) {
            actor.enterStep(uint8_t(FightCruiseStep::PickHeading));
            ship.behaviour &= uint8_t(~kBehaveStraight);
        }
        break;
    }
    case FightCruiseStep::PauseStart:
        ship.timer = fightRunTicks[rngStep() & 0xf];
        actor.enterStep(uint8_t(FightCruiseStep::Pause));
        [[fallthrough]];
    case FightCruiseStep::Pause: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.enterStep(uint8_t(FightCruiseStep::RunStart));
        break;
    }
    default: break;
    }
    // exe 0x59f850(actor, the player's position copied on the stack, 0x180, 0x10)
    if ((ship.behaviour & kBehaveStraight) || steerToward(actor, player().pos, 0x180, 0x10) == 0)
        steerDamp(actor, 0x10);
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x400) != 0) actor.velocity.z = -0x400;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    const FightOrders orders = FightOrders(ship.orders.get());
    if (orders == FightOrders::ToSquare) {
        if (planarDistance(actor.pos, kSquareCentre) < kSquareRadius) {
            actor.subState = uint8_t(FightGunshipSub::Leave);
            actor.enterStep(0);
        }
    } else if (orders == FightOrders::Now) {
        actor.enterStep(0);
        actor.subState = uint8_t(FightGunshipSub::Leave);
    }
}

// exe 0x59e8f0: the level pitch, the cruise speed, the rates cleared, then the cruise
void fightCruiseStart(StaticActor& actor, FightGunshipFields& ship) {
    ship.pitchTarget = kLevelPitch;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = -0x400;
    actor.acceleration.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0;
    actor.subState = uint8_t(FightGunshipSub::Cruise);
    actor.enterStep(0);
    fightCruise(actor, ship);
}

// exe 0x59ec30: climbs in from the edge on an orbit around the centre to y -0xc80, levels, then circles (variants 0 /
// 1) or the square's circle
void fightClimbIn(StaticActor& actor, FightGunshipFields& ship) {
    ship.behaviour |= kBehaveLevelA;
    const int ang = bearingFromCentre(actor);
    switch (ClimbInStep(actor.step)) {
    case ClimbInStep::Init:
        ship.flight = 0;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~kBehaveNoPushOut)) | kBehaveLevel);
        ship.pitchTarget = kLevelPitch;
        actor.velocity.y = -0x100;
        actor.velocity.z = -0x400;
        actor.acceleration.setXY(0, 0);
        actor.acceleration.z = kSpeedStep;
        actor.spin.x = 0;
        ship.orbit = int16_t((rngStep() & 1) ? 0x4000 : 0xc000);
        actor.enterStep(uint8_t(ClimbInStep::Climb));
        [[fallthrough]];
    case ClimbInStep::Climb:
        if (actor.pos.y.whole <= -0xc80) actor.enterStep(uint8_t(ClimbInStep::Level));
        break;
    case ClimbInStep::Level:
        actor.velocity.y = int16_t(actor.velocity.y + 0x10);
        if (actor.velocity.y < 0) break;
        actor.velocity.y = 0;
        actor.subState = uint8_t(actor.variant < 2 ? FightGunshipSub::CruiseStart : FightGunshipSub::Circle);
        actor.enterStep(0);
        break;
    default: break;
    }
    actor.rotation.y = int16_t(ang + (ship.orbit >> 4));
    integrateVelocity(actor, 0, actor.velocity.y, -0x400);
}

void fightFlyOff(StaticActor& actor, FightGunshipFields& ship);

// exe 0x59ed60: circles the square's centre (in to half its distance + 0x400, the orbit turned around), dropping a
// Birdbot (a free track id) at each quarter of the circle crossed while the drop cooldown is out; no free id: the
// orders cleared and the circle held until told again; told now: fly off
void fightCircle(StaticActor& actor, FightGunshipFields& ship) {
    if (FightOrders(ship.orders.get()) == FightOrders::Now) {
        actor.subState = uint8_t(FightGunshipSub::FlyOff);
        actor.enterStep(0);
        fightFlyOff(actor, ship);
        return;
    }
    const int ang = bearingFromCentre(actor);
    int32_t behind[3];
    switch (CircleStep(actor.step)) {
    case CircleStep::Init:
        ship.flight = 0;
        ship.orbitStep = int16_t(ship.orbit <= 0 ? 0x60 : 0xa0);
        ship.orbitRadius = int16_t((planarDistance(actor.pos, kSquareCentre) + kSquareRadius) >> 1);
        actor.enterStep(uint8_t(CircleStep::Approach));
        [[fallthrough]];
    case CircleStep::Approach: {
        const int16_t v = ship.orbit;
        if ((v < 0 ? -v : v) > 0x2000) ship.orbit = int16_t(v + ship.orbitStep);
        if (planarDistance(actor.pos, kSquareCentre) < ship.orbitRadius) actor.enterStep(uint8_t(CircleStep::Turn));
        break;
    }
    case CircleStep::Turn: {
        ship.orbit = int16_t(ship.orbit - ship.orbitStep);
        const int16_t v = ship.orbit;
        if ((v < 0 ? -v : v) < 0x4000) break;
        ship.orbit = int16_t(v <= 0 ? 0xc000 : 0x4000);
        ship.turnMargin = 0;
        pointBehind(actor, behind);
        ship.lastBearing = int16_t(bearing(kSquareCentre, behind));
        actor.enterStep(uint8_t(CircleStep::Orbit));
        break;
    }
    case CircleStep::Orbit: {
        pointBehind(actor, behind);
        const int b = bearing(kSquareCentre, behind);
        const uint32_t q = uint32_t(b + 0x200) & 0xc00;
        const int e = angleDiff(uint32_t(b), q);
        if ((e == 0 || (angleDiff(uint32_t(int32_t(ship.lastBearing)), q) ^ e) < 0) &&
            ship.attackCooldown == 0) {
            ShipFightBlock& block = shipFightBlock();
            uint32_t m = block.freeIds, id = 1;
            bool freeId = false;
            for (; m != 0; m >>= 1, ++id) {
                if (m & 1) {
                    freeId = true;
                    break;
                }
            }
            if (freeId) {
                if (dropBirdbot(actor, 0, uint16_t(q), id) != 0) block.dropped += 1u;
                ship.attackCooldown = dropCooldowns[rngStep() & 0xf];
            } else {
                ship.orders = int8_t(FightOrders::None);
                actor.enterStep(uint8_t(CircleStep::Hold));
            }
        }
        ship.lastBearing = int16_t(b);
        orbitRadiusCorrection(actor, ship);
        break;
    }
    case CircleStep::Hold:
        if (FightOrders(ship.orders.get()) != FightOrders::None) {
            pointBehind(actor, behind);
            ship.lastBearing = int16_t(bearing(kSquareCentre, behind));
            actor.enterStep(uint8_t(CircleStep::Orbit));
        }
        orbitRadiusCorrection(actor, ship);
        break;
    default: break;
    }
    actor.rotation.y = int16_t(ang + int(ship.turnMargin) + (ship.orbit >> 4));
    integrateVelocity(actor, 0, 0, -0x400);
}

// exe 0x59f100: away from the centre beyond 0x1300, then climbing and turning off; above y -0x600 -> freed
void fightFlyOff(StaticActor& actor, FightGunshipFields& ship) {
    const int ang = bearingFromCentre(actor);
    switch (FlyOffStep(actor.step)) {
    case FlyOffStep::Init:
        ship.flight = 0;
        actor.velocity.y = 0;
        actor.spin.y = 0;
        actor.enterStep(uint8_t(FlyOffStep::Out));
        ship.orbit = int16_t((rngStep() & 1) ? 0x400 : 0xfc00);
        [[fallthrough]];
    case FlyOffStep::Out:
        if (planarDistance(actor.pos, kSquareCentre) > 0x1300) actor.enterStep(uint8_t(FlyOffStep::Climb));
        break;
    case FlyOffStep::Climb:
        actor.velocity.y = int16_t(actor.velocity.y + 0x10);
        if (actor.velocity.y > 0x100) actor.velocity.y = 0x100;
        actor.spin.y = int16_t(actor.spin.y + 0x10);
        if (actor.spin.y > 0x180) actor.spin.y = 0x180;
        turnYawTo(actor.rotation.y, uint32_t(ship.orbit + ang), actor.spin.y >> 4); // exe 0x5e3df0
        break;
    default: break;
    }
    if (stepTowardLimit(actor.velocity.z, 0xffe0, 0x400) != 0) actor.velocity.z = -0x400; // exe 0x5e5510
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (actor.pos.y.whole > -0x600) actor.enterState(uint8_t(FightGunshipState::Gone));
}

// exe 0x59f230: out beyond 0xb00 shooting, then turned back in (the square's circle)
void fightLeave(StaticActor& actor, FightGunshipFields& ship) {
    const int ang = bearingFromCentre(actor);
    switch (LeaveStep(actor.step)) {
    case LeaveStep::Init:
        ship.flight = 0;
        actor.velocity.y = 0;
        actor.spin.y = 0;
        ship.orbit = int16_t((rngStep() & 1) ? 0x4000 : 0xc000);
        ship.timer = 0;
        actor.enterStep(uint8_t(LeaveStep::Out));
        [[fallthrough]];
    case LeaveStep::Out: {
        if (planarDistance(actor.pos, kSquareCentre) > 0xb00) actor.enterStep(uint8_t(LeaveStep::TurnBack));
        const uint8_t t = ship.timer;
        if (!(t & 3)) ship.flight |= (t & 7) ? kGunLeft : kGunRight;
        ship.timer = uint8_t(t + 1);
        break;
    }
    case LeaveStep::TurnBack: {
        actor.spin.y = int16_t(actor.spin.y + 0x10);
        if (actor.spin.y > 0x180) actor.spin.y = 0x180;
        const int step = actor.spin.y >> 4;
        const int r = turnYawTo(actor.rotation.y, uint32_t((ship.orbit >> 4) + ang), step);
        if (uint32_t(r + step) < uint32_t(step * 2)) {
            actor.spin.y = 0;
            actor.subState = uint8_t(FightGunshipSub::Circle);
            actor.enterStep(0);
        }
        break;
    }
    default: break;
    }
    if (stepTowardLimit(actor.velocity.z, 0xffe0, 0x400) != 0) actor.velocity.z = -0x400; // exe 0x5e5510
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}

// exe 0x59e770: the model, the hit points of its row, the box, airborne, the home height above it; a variant != 0
// starts climbing in
void fightInit(StaticActor& actor, FightGunshipFields& ship) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    const int32_t y = actor.pos.y.whole;
    actor.collisionBox.pointAt(fightGunshipBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    ship.homeHeight = y + kHomeAbove;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(FightGunshipState::Alive));
    if (actor.variant != 0) actor.subState = uint8_t(FightGunshipSub::ClimbIn);
}

// exe 0x59e810
void fightAlive(const PlayerEnv& env, StaticActor& actor, FightGunshipFields& ship) {
    const GunshipHit r = gunshipHits(actor);
    if (r == GunshipHit::Strong) {
        if (!(ship.flight & kFlightNoRock) && !(ship.behaviour & kBehaveRocking)) {
            ship.behaviour |= kBehaveRocking;
            rockInit(actor);
        }
    } else if (r == GunshipHit::Dead) {
        actor.enterState(uint8_t(FightGunshipState::Crash));
        gunshipCrash(env, actor, 0x200, true);
        return;
    }
    switch (FightGunshipSub(actor.subState)) {
    case FightGunshipSub::CruiseStart: fightCruiseStart(actor, ship); break;
    case FightGunshipSub::Cruise: fightCruise(actor, ship); break;
    case FightGunshipSub::ClimbIn: fightClimbIn(actor, ship); break;
    case FightGunshipSub::Circle: fightCircle(actor, ship); break;
    case FightGunshipSub::FlyOff: fightFlyOff(actor, ship); break;
    case FightGunshipSub::Leave: fightLeave(actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x59e630 sub-state", actor.subState);
    }
    if (ship.behaviour & kBehaveRocking) {
        ship.flight |= kFlightRocking;
        if (rock(actor) != 0) {
            ship.behaviour &= uint8_t(~kBehaveRocking);
            ship.flight &= uint8_t(~kFlightRocking);
        }
    }
    if (ship.flight & kFlightSway) sway(actor);
    attitude(actor);
    fightCollide(env, actor, ship);
    if (ship.attackCooldown != 0) ship.attackCooldown -= 1u;
}

// exe 0x59e630
void shipFightGunship(const PlayerEnv& env, StaticActor& actor) {
    FightGunshipFields& ship = classState<FightGunshipFields>(actor);
    const uint32_t statRow = statTableRow(actorStatTables().statics, actor.type);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (FightGunshipState(actor.state)) {
    case FightGunshipState::Init: fightInit(actor, ship); break;
    case FightGunshipState::Alive: fightAlive(env, actor, ship); break;
    case FightGunshipState::Crash: gunshipCrash(env, actor, 0x200, true); break; // exe 0x59f380
    case FightGunshipState::Gone: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "static 0x59e630 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    gunshipFireAsked(actor, 0x80, GunshipGun::Mission); // exe 0x59f5b0
    const uint8_t flight = ship.flight;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    ship.hitBox.result = 0;
    ship.hitBox.previousX = actor.prevPos.x.whole;
    ship.hitBox.previousZ = actor.prevPos.z.whole;
    const uint32_t off = ((flight & kFlightDown) || ship.shielded != 0) ? kHitNoLockOn : 0u;
    const StaticActorStats row = staticStatsAt(statRow);
    hitListSubmit(p, ship.hitBox,
                  hitOwner(actor.slot) | hitHeightLevel(row.hitHeight) | off | hitGroup(kHitGroupBodies) | hitRadius(0xc8),
                  row.damage | kHitAttackSolid); // exe 0x5e4120
    // exe 0x5f20f0(a, 0xc0, 0x100, 0): the shadow list entry (render output)
}

} // namespace

void cutsceneGunshipUpdate(const PlayerEnv&, uint32_t actor) { cutsceneGunship(staticActorAt(actor)); }
void shipFightGunshipUpdate(const PlayerEnv& env, uint32_t actor) { shipFightGunship(env, staticActorAt(actor)); }

} // namespace od::port
