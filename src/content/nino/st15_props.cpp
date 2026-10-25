// The static classes of Nino island's fields (ST15 / ST17 / ST19), one routine per exe routine. Names are hypotheses
// from the observed behaviour. An entry past a class's own states (the exe's tables run into the next class's tables)
// is a port gap.
//
// 0x58ef40 (ST15; states [0x8cdb94]: 0 0x58ef90 (model by the variant, box 0x8cdb88, the hit points of its row), 1
//   0x58f020 (variants [0x8cdb9c]: 0 0x52ef00), 2 0x52ef00).
// 0x5a2620 (ST15; states [0x8ceba8]: 0 0x5a26b0, 1 0x5a2740 (sub-states [0x8cebb8]: 0 0x5a27c0, 1 0x5a2800 circle the
//   point 0x8cebc0 (radius 0x240); flag 0x690 -> state 2), 2 0x5a28e0 crash, 3 freed) and 0x5a2060 (ST19; states
//   [0x8ceb70]: 0 0x5a20f0, 1 0x5a21a0 (sub-states [0x8ceb80]: 0 0x5a2220 by the variant, 1 0x5a22a0 climb, 2 0x5a22f0
//   by the script's steps (step sub 1 climb, 2 a slow climb, scale 0x200 at its tick 1)), 2 0x5a2380 crash, 3 freed):
//   drones of the gunship family (game/gunship_flight); their guns (0x5a2b00: the enemy shot).
// 0x5a3910 (ST17; states [0x8cec4c]: 0 0x5a3b60 (boxes 0x8cec30 / 0x8cec40, the hit points of its row * 8, id 7's defeat
//   cleared), 1 0x5a3c00 (hits: strong ones rock it (sub-states [0x8cec58]: 0 0x52ef00, 1 0x5a3d90), fires (static type
//   0x26 at the points 0x8cec60 / 68 / 70) at 1/4 and 1/8 of its hit points; it bobs; dead -> state 2 once the player
//   leaves the arena), 2 0x5a3e50 (the fall: two cell segments switched, the fires put out, an e0 type 0x1b, id 8
//   defeated, blasts and smoke every 4 ticks for 0x5a ticks, then a blast, id 7 defeated, freed), 3 0x52ef00). Its
//   head: 3 hit capsules + the solid.
// 0x5af870 (ST19; states [0x8cf9ec]: 0 0x5af8e0 still solid (boxes 0x8cf9d0 / 0x8cf9e0, 1 hit point), 1 0x52ef00, 2
//   0x5af9c0 (the hittable target: model by the variant, the hit points of its row * 2, its regeneration timer 0x3c), 3
//   0x5afa50 (hits: the damage meter, every overflow one hit point off and a jolt; sub-states [0x8cfa00]: 0 0x5afbd0
//   (one hit point back every 0x3c ticks; the jolt toward the camera), 1 0x5afcf0 (out of hit points: the jolt, then
//   falls)), 4 freed). Its head: the solid.
//
// Left out (render output): the shadow exe 0x5f20f0 of 0x5af870.
// Exe anomaly: 0x5afa50 writes its capsule's first point at [ESP + 0x1c..0x21] -- the z word lands on the low word of
// its own return address (SUB ESP, 0x10 + 4 pushes); the exe could not return from it. The port returns normally.
#include "content/nino/st15_props.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/st15_props_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "content/nino/ch2_rooms.hpp"
#include "game/gunship_flight.hpp"

#include "game/actor_collision.hpp"
#include "game/fixed_point.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera_state.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st15_props_state;

namespace {

// ==== 0x58ef40 (ST15): the still model =============================================================================
// [0x8cdb94]
enum class StillState : uint8_t { Init = 0, Stand = 1, Idle = 2 };
const DataTable<CollisionBoxRecord> st15StillBox{TableId::St15PropsStillBox};

// exe 0x58ef40
void st15Still(StaticActor& actor) {
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (StillState(actor.state)) {
    case StillState::Init: { // exe 0x58ef90
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
        actor.velocity.clearWords();
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.maxHitPoints = hp;
        actor.hitPoints = hp;
        actor.collisionBox.pointAt(st15StillBox.stateAddress());
        actor.clearClassBlock();
        actor.enterState(uint8_t(StillState::Stand));
        break;
    }
    case StillState::Stand: // exe 0x58f020: by the variant [0x8cdb9c]
        if (actor.variant != 0) notPortedAt(kStoryPort, "static 0x58ef40 variant", actor.variant);
        break;
    case StillState::Idle: break; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "static 0x58ef40 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    actor.wasOnPlatform = actor.onPlatform;
}

// ==== 0x5a2620 (ST15) / 0x5a2060 (ST19): the drones =====================================================================
// [0x8ceba8] / [0x8ceb70]
enum class DroneState : uint8_t { Init = 0, Fly = 1, Crash = 2, Gone = 3 };
// [0x8cebb8]
enum class PatrolSub : uint8_t { Start = 0, Circle = 1 };
// [0x8ceb80]
enum class CutsceneDroneSub : uint8_t { Start = 0, Climb = 1, Scripted = 2 };

const DataTable<CollisionBoxRecord> patrolDroneBox{TableId::St15PropsPatrolDroneBox};
const DataTable<CollisionBoxRecord> cutsceneDroneBox{TableId::St15PropsCutsceneDroneBox};
// the point the patrol circles (exe data 0x8cebc0: 16.16 x / y / z)
struct PatrolCentre : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int32_t x = 0, y = 0, z = 0;
    static PatrolCentre load(const TableReader& r) { return {{}, r.s32(0), r.s32(4), r.s32(8)}; }
};
const DataTable<PatrolCentre> patrolCentre{TableId::St15PatrolCentre};
constexpr uint32_t kFlagPatrolDown = 0x690;
constexpr int8_t kLevelPitch = 0x30;
constexpr int16_t kSpeedStep = -0x20;
constexpr int16_t kSmallScale = 0xc0, kFullScale = 0x200;

// the drones' common init after the model: the hit points of the variant-1 row * 8, no motion, the box, airborne, the
// class block cleared
void droneInit(StaticActor& actor, const DataTable<CollisionBoxRecord>& box) {
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
    actor.collisionBox.pointAt(box.stateAddress());
    actor.airborne = 1;
    actor.clearClassBlock(); // exe 0x40dc40
}
// the drones' head: the tint, the previous pose and contact
void droneHead(StaticActor& actor, GunshipFields& ship) {
    ship.resetTintBytes();
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    actor.wasOnPlatform = actor.onPlatform;
}
// the drones' tail: the guns asked for
void droneGuns(StaticActor& actor) {
    if (!(actor.flags & kActorLive)) return;
    gunshipFireAsked(actor, 0x60, GunshipGun::Drone); // exe 0x5a2b00
}
// the rocking of a strong hit and the sway
void droneRockSway(StaticActor& actor, GunshipFields& ship) {
    if (ship.behaviour & kBehaveRocking) {
        ship.flight |= kFlightRocking;
        if (rock(actor) != 0) {
            ship.behaviour &= uint8_t(~kBehaveRocking);
            ship.flight &= uint8_t(~kFlightRocking);
        }
    }
    if (ship.flight & kFlightSway) sway(actor);
}

// exe 0x5a2800: circles the point 0x8cebc0 at -0x400; flag 0x690 -> the crash
void patrolCircle(StaticActor& actor, GunshipFields& ship) {
    if (actor.step == 0) {
        ship.flight = 0;
        ship.pitchTarget = kLevelPitch;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(1);
    } else if (actor.step != 1) {
        notPortedAt(kStoryPort,
                    "static 0x5a2620: 0x5a2800 with +0xa > 1 (the exe reads its point from uninitialised stack)",
                    actor.step);
    }
    // exe 0x5a2c90(actor, point, 0x240, 0x18): steered toward the bearing from the point (a stack copy of it)
    if (steerToward(actor, patrolCentre->x, patrolCentre->z, 0x240, 0x18) == 0) steerDamp(actor, 0x18);
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x400) != 0) actor.velocity.z = -0x400; // 0x5e5510
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (!storyFlag(kFlagPatrolDown)) return;
    actor.enterState(uint8_t(DroneState::Crash));
}

// exe 0x5a2620
void patrolDrone(const PlayerEnv& env, StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    droneHead(actor, ship);
    switch (DroneState(actor.state)) {
    case DroneState::Init: // exe 0x5a26b0
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        droneInit(actor, patrolDroneBox);
        actor.enterState(uint8_t(DroneState::Fly));
        break;
    case DroneState::Fly: // exe 0x5a2740
        switch (PatrolSub(actor.subState)) {
        case PatrolSub::Start: // exe 0x5a27c0
            ship.pitchTarget = kLevelPitch;
            actor.velocity.setXY(0, 0);
            actor.velocity.z = -0x400;
            actor.acceleration.setXY(0, 0);
            actor.spin.x = 0;
            actor.spin.y = 0;
            actor.subState = uint8_t(PatrolSub::Circle);
            actor.enterStep(0);
            patrolCircle(actor, ship);
            break;
        case PatrolSub::Circle: patrolCircle(actor, ship); break;
        default: notPortedAt(kStoryPort, "static 0x5a2620 sub-state", actor.subState);
        }
        droneRockSway(actor, ship);
        attitude(actor); // exe 0x59e420
        break;
    case DroneState::Crash: gunshipCrash(env, actor, 0x200, false); break; // exe 0x5a28e0
    case DroneState::Gone: freeActor(actor); break;                       // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x5a2620 state", actor.state);
    }
    droneGuns(actor);
}

// exe 0x5a22a0: climbs forward at -0x100
void cutsceneDroneClimb(StaticActor& actor, GunshipFields& ship) {
    if (actor.step == 0) {
        ship.flight = 0;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(1);
    }
    integrateVelocity(actor, 0, 0, -0x100);
}

// exe 0x5a22f0: by the script's step sub: 1 the climb; 2 a slow climb (full size at the step's tick 1)
void cutsceneDroneScripted(StaticActor& actor, GunshipFields& ship) {
    const ScriptState& s = scriptState();
    if (s.stepSub == 1) {
        cutsceneDroneClimb(actor, ship);
        return;
    }
    if (s.stepSub != 2) return;
    if (s.stepTick == 1) {
        actor.scale.z = kFullScale;
        actor.scale.y = kFullScale;
        actor.scale.x = kFullScale;
    }
    integrateVelocity(actor, 0, 0, -0x40);
}

// exe 0x5a2060
void cutsceneDrone(const PlayerEnv& env, StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    droneHead(actor, ship);
    switch (DroneState(actor.state)) {
    case DroneState::Init: // exe 0x5a20f0
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        droneInit(actor, cutsceneDroneBox);
        actor.scale.z = kSmallScale;
        actor.scale.y = kSmallScale;
        actor.scale.x = kSmallScale;
        actor.enterState(uint8_t(DroneState::Fly));
        break;
    case DroneState::Fly: // exe 0x5a21a0
        switch (CutsceneDroneSub(actor.subState)) {
        case CutsceneDroneSub::Start: // exe 0x5a2220: by the variant
            actor.spin.x = 0;
            actor.spin.y = 0;
            ship.flight = 0;
            ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
            if (actor.variant == 0) {
                ship.cooldown = 0;
                actor.velocity.setXY(0, 0);
                actor.velocity.z = -0x100;
                actor.acceleration.setXY(0, 0);
                actor.subState = uint8_t(CutsceneDroneSub::Climb);
                actor.enterStep(0);
                cutsceneDroneClimb(actor, ship);
            } else if (actor.variant == 1) {
                ship.cooldown = 0x100;
                actor.velocity.setXY(0, 0);
                actor.velocity.z = -0x100;
                actor.acceleration.setXY(0, 0);
                actor.subState = uint8_t(CutsceneDroneSub::Scripted);
                actor.enterStep(0);
                cutsceneDroneScripted(actor, ship);
            }
            break;
        case CutsceneDroneSub::Climb: cutsceneDroneClimb(actor, ship); break;
        case CutsceneDroneSub::Scripted: cutsceneDroneScripted(actor, ship); break;
        default: notPortedAt(kStoryPort, "static 0x5a2060 sub-state", actor.subState);
        }
        droneRockSway(actor, ship);
        rollToTurn(actor); // exe 0x5a25a0
        break;
    case DroneState::Crash: gunshipCrash(env, actor, 0x100, false); break; // exe 0x5a2380
    case DroneState::Gone: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "static 0x5a2060 state", actor.state);
    }
    droneGuns(actor);
}

// A fire on the machine (stage static type 0x26): its owner's slot, put out, its size; its place relative to the owner
struct MachineFireParams {
    explicit MachineFireParams(const StaticActor& actor) : MachineFireParams(const_cast<StaticActor&>(actor)) {}
    explicit MachineFireParams(StaticActor& actor) : owner(actor.params[0]), putOut(actor.params[1]), size(actor.params[2]) {}
    uint8_t& owner;
    uint8_t& putOut;
    uint8_t& size;
};
// A fire's place on the machine (exe data: z, yaw, y)
struct FirePlace : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    int16_t z = 0; // +0x0
    int16_t yaw = 0; // +0x2
    int16_t y = 0; // +0x4
    template <class R> static FirePlace load(const R& r) {
        FirePlace v;
        v.z = r.template read<int16_t>(0x0);
        v.yaw = r.template read<int16_t>(0x2);
        v.y = r.template read<int16_t>(0x4);
        return v;
    }
};

// [0x8cec4c]
enum class HoverMachineState : uint8_t { Init = 0, Alive = 1, Dying = 2, Idle = 3 };
// [0x8cec58]
enum class HoverMachineSub : uint8_t { Steady = 0, Rocked = 1 };
enum class FireStage : uint8_t { None = 0, One = 1, Three = 2 };
enum class DyingSub : uint8_t { Start = 0, Fall = 1 };

const DataTable<CollisionBoxRecord> hoverMachineBox{TableId::St15PropsHoverMachineBox};
const DataTable<InteractionBoxRecord> hoverMachineTalkBox{TableId::St15PropsHoverMachineTalkBox};
constexpr TableId kFirePlaces[3] = {TableId::St15PropsFirstFirePlace, TableId::St15PropsSecondFirePlace, TableId::St15PropsThirdFirePlace};
const DataTable<ScalarTable<int16_t, 16>> blastSpread{TableId::St15PropsBlastSpread};
constexpr uint8_t kFireType = 0x26, kFireSize = 0x1e, kTimerType = 0x1b;
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint32_t kIdMachine = 7, kIdMachineDown = 8;
constexpr uint8_t kGridRow = 0x3e, kGridColumnA = 0x3c, kGridColumnB = 0x3e;
// the arena the player must leave before the dead machine falls (16.16)
constexpr int32_t kArenaMinX = -0x6400000, kArenaMaxX = 0, kArenaMinZ = -0x5000000, kArenaMaxZ = 0x800000;

// exe 0x5a4140(actor, place): a fire (stage static type 0x26) at a place on the machine; its slot
uint8_t lightFire(const StaticActor& actor, const FirePlace& place) {
    const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!s) return 0;
    StaticActor& fire = staticActorAt(s);
    auto&& params = MachineFireParams(staticActorAt(s));
    fire.loopFlags |= kActorSweepGroup2;
    fire.flags = kActorLive;
    fire.type = kFireType;
    fire.subtype = 0;
    fire.variant = 0;
    params.putOut = 0;
    fire.pos.x.whole = 0;
    params.owner = actor.slot;
    params.size = kFireSize;
    fire.pos.y.whole = place.y;
    fire.pos.z.whole = place.z;
    fire.rotation.y = place.yaw;
    return fire.slot;
}

// exe 0x5a3d90: the rocking of a strong hit; settled -> steady
void machineRock(StaticActor& actor) {
    int16_t& rollSpeed = actor.acceleration.x;
    if (actor.step == 0) {
        actor.enterStep(1);
        actor.spin.z = 0x200;
        rollSpeed = 0x200;
    }
    int16_t amp = actor.spin.z;
    const uint32_t r = uint16_t(actor.rotation.z) & 0xfff;
    actor.rotation.z = int16_t((rollSpeed >> 4) + uint16_t(actor.rotation.z));
    if (r <= 0x7ff) rollSpeed = int16_t(rollSpeed - 0x100);
    if (r >= 0x801) rollSpeed = int16_t(rollSpeed + 0x100);
    if (int(amp) < int(rollSpeed)) rollSpeed = amp;
    if (int(rollSpeed) < -int(amp)) rollSpeed = int16_t(-amp);
    if ((uint16_t(actor.prevRotation.z) ^ uint16_t(actor.rotation.z)) & 0x800) {
        amp = int16_t(amp - (amp >> 2));
        actor.spin.z = amp;
        if (uint16_t(amp + 0x40) <= 0x7f) {
            actor.subState = uint8_t(HoverMachineSub::Steady);
            actor.enterStep(0);
        }
    }
}

// exe 0x5a3c00: the hits (a strong one rocks it), the fires at a quarter / an eighth of its hit points, the rocking, the
// bob; dead and the player out of the arena -> the fall
void machineAlive(StaticActor& actor, HoverMachineFields& machine) {
    const uint32_t n = machine.hitBox.result & kHitDamageMask;
    if (n != 0 && machine.hurtTicks == 0) {
        uint16_t snd = kHurtSound;
        actor.tint = kTintFlash;
        if (machine.hitBox.result & kHitAttackStrongPush) {
            machine.hurtTicks = 0xf;
            snd = kStrongHurtSound;
            if (HoverMachineSub(actor.subState) != HoverMachineSub::Rocked) {
                actor.subState = uint8_t(HoverMachineSub::Rocked);
                actor.enterStep(0);
            }
        }
        soundPlayAtPos(snd, actor.pos); // exe 0x5d5380
        if (hitPointsAdd(actor, -int(n)) != 0) machine.dead = 1;
        if (FireStage(machine.fireStage) == FireStage::None) {
            if (actor.hitPoints <= (actor.maxHitPoints >> 2)) {
                machine.fires[0] = lightFire(actor, dataRecord<FirePlace>(stateAddress(kFirePlaces[0]), FirePlace::kBytes));
                machine.fireStage = uint8_t(FireStage::One);
            }
        } else if (FireStage(machine.fireStage) == FireStage::One &&
                   actor.hitPoints <= (actor.maxHitPoints >> 3)) {
            machine.fires[1] = lightFire(actor, dataRecord<FirePlace>(stateAddress(kFirePlaces[1]), FirePlace::kBytes));
            machine.fires[2] = lightFire(actor, dataRecord<FirePlace>(stateAddress(kFirePlaces[2]), FirePlace::kBytes));
            machine.fireStage = uint8_t(FireStage::Three);
        }
    }
    if (machine.hurtTicks != 0) machine.hurtTicks -= 1u;
    switch (HoverMachineSub(actor.subState)) {
    case HoverMachineSub::Steady: break; // exe 0x52ef00
    case HoverMachineSub::Rocked: machineRock(actor); break;
    default: notPortedAt(kStoryPort, "static 0x5a3910 sub-state", actor.subState);
    }
    actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
    if (actor.velocity.y >= 0x21) {
        actor.velocity.y = 0x20;
        actor.acceleration.y = -2;
    }
    if (actor.velocity.y <= -0x21) {
        actor.velocity.y = -0x20;
        actor.acceleration.y = 2;
    }
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(int32_t(actor.velocity.y) * 0x1000));
    const Fixed16Vec3& p = player().pos;
    if (machine.dead != 0 && (p.x < kArenaMinX || p.x > kArenaMaxX || p.z < kArenaMinZ || p.z > kArenaMaxZ))
        actor.enterState(uint8_t(HoverMachineState::Dying));
}

// a blast (the global static type 7) at a 16.16 point
void blastAt(int32_t x, int32_t y, int32_t z, uint16_t size) {
    const uint32_t s = allocActor60(0); // exe 0x5e1300
    if (!s) return;
    StaticActor& blast = staticActorAt(s);
    auto&& params = BlastParams(actorHeaderAt(s));
    blast.flags = kActorLive;
    blast.loopFlags |= kActorSweepGroup2;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.subtype = 0;
    blast.variant = 0;
    blast.pos.x = x;
    params.size = size;
    params.attack = 0;
    blast.pos.y = y;
    blast.pos.z = z;
}
// a smoke (the stage's static type 3) at a 16.16 point
void smokeAt(int32_t x, int32_t y, int32_t z) {
    const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!s) return;
    StaticActor& smoke = staticActorAt(s);
    auto&& params = SmokeParams(actorHeaderAt(s));
    smoke.flags = kActorLive;
    smoke.pos.x = x;
    smoke.type = kSmokeType;
    smoke.variant = 0;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.pos.y = y;
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 4;
    smoke.pos.z = z;
}

// exe 0x5a3e50: the fall -- its two grid cells' segments switched, the fires put out, the e0 timer, id 8 defeated; every
// 4th tick a blast and smoke around it; it rolls over and falls; at the end a last blast, id 7 defeated, gone
void machineDying(StaticActor& actor, HoverMachineFields& machine) {
    const DyingSub sub = DyingSub(actor.subState);
    if (uint8_t(sub) > uint8_t(DyingSub::Fall)) return;
    if (sub == DyingSub::Start) {
        switchCellSegmentVariant(ninoGridCell(kGridColumnA, kGridRow), 1); // exe 0x58d5f0
        switchCellSegmentVariant(ninoGridCell(kGridColumnB, kGridRow), 1);
        actor.velocity.y = 0;
        actor.spin.x = 0;
        actor.rotation.z = 0;
        actor.subState = uint8_t(DyingSub::Fall);
        for (int k = 3; k >= 0; --k) { // the fires put out
            const uint32_t sl = machine.fires[size_t(k)];
            if (sl == 0) continue;
            StaticActor& fire = staticPool()[size_t(sl - kStaticClass)];
            if (fire.flags & kActorLive) MachineFireParams(fire).putOut = 1;
        }
        if (const uint32_t e = allocActorE0()) { // exe 0x5e1440
            ControllerActor& timer = controllerActorAt(e);
            auto&& params = StageTimerParams(controllerActorAt(e));
            timer.flags = kActorLive;
            timer.type = kTimerType;
            timer.variant = 0;
            params.value = 0x23;
            params.limit = 0x7f;
            params.flag = -1;
        }
        enemyMarkDefeated(kIdMachineDown); // exe 0x5e63c0
        machine.timer = 0x5a;
    }
    if (!(machine.timer & 3)) {
        const int32_t x = int32_t((uint32_t(int32_t(blastSpread[rngStep() & 0xf])) << 16) + uint32_t(actor.pos.x));
        const int32_t y = int32_t(uint32_t(actor.pos.y) - 0x640000u);
        const int32_t z = int32_t((uint32_t(int32_t(blastSpread[rngStep() & 0xf])) << 16) + uint32_t(actor.pos.z));
        blastAt(x, y, z, 0x40);
        smokeAt(x, y, z);
        smokeAt(actor.pos.x, actor.pos.y, actor.pos.z);
    }
    actor.spin.x = int16_t(actor.spin.x + 8);
    if (actor.spin.x > 0x200) actor.spin.x = 0x200;
    actor.rotation.z = int16_t(actor.rotation.z - (actor.spin.x >> 4));
    if (actor.rotation.z < -0x380) actor.rotation.z = -0x380;
    if (actor.rotation.z < -0x40) actor.velocity.y = int16_t(actor.velocity.y + 0x10);
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
    const uint8_t t = uint8_t(machine.timer - 1);
    machine.timer = t;
    if (t != 0) return;
    blastAt(actor.pos.x, actor.pos.y, actor.pos.z, 0x200);
    enemyMarkDefeated(kIdMachine);
    freeActor(actor);
}

// the hit capsule's ends around the machine: at the angle and radius from its centre, dy below
void capsuleEnd(const StaticActor& actor, int ang, int r, int dy, int16_t out[3]) { // exe 0x5e40c0
    int16_t dx, dz;
    angleVector(ang, r, dx, dz);
    out[0] = int16_t(dx + actor.pos.x.whole);
    out[1] = int16_t(actor.pos.y.whole + dy);
    out[2] = int16_t(dz + actor.pos.z.whole);
}

// exe 0x5a3910
void hoverMachine(StaticActor& actor) {
    auto&& machine = classState<HoverMachineFields>(actor);
    actor.tint = kTintNeutral; // the tint word reset (the gunships' step, not their class state)
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (HoverMachineState(actor.state)) {
    case HoverMachineState::Init: { // exe 0x5a3b60
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            break;
        }
        actor.flags |= kActorScenery;
        const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints << 3) : int16_t(0);
        actor.maxHitPoints = hp;
        actor.hitPoints = hp;
        actor.velocity.setXY(0, 0);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.interaction = 0;
        actor.acceleration.y = -2;
        actor.collisionBox.pointAt(hoverMachineBox.stateAddress());
        actor.interactionBox.pointAt(hoverMachineTalkBox.stateAddress());
        actor.clearClassBlock();
        actor.enterState(uint8_t(HoverMachineState::Alive));
        enemyClearDefeated(kIdMachine); // exe 0x5e63f0
        break;
    }
    case HoverMachineState::Alive: machineAlive(actor, machine); break;
    case HoverMachineState::Dying: machineDying(actor, machine); break;
    case HoverMachineState::Idle: break; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "static 0x5a3910 state", actor.state);
    }
    actor.wasOnPlatform = actor.onPlatform;
    if (!(actor.flags & kActorLive)) return;
    machine.hitBox.result = 0;
    machine.hitBox.previousX = actor.prevPos.x.whole;
    machine.hitBox.previousZ = actor.prevPos.z.whole;
    const int yaw = actor.rotation.y;
    int16_t p[3], q[3];
    const uint32_t owner = hitOwner(actor.slot);
    constexpr uint32_t kBodyAttack = kHitAttackHarmless | kHitAttackSolid;
    // the body (heights 3) and the two wings (heights 1) as capsules (exe 0x5e4190)
    capsuleEnd(actor, yaw, 0x280, 0, p);
    capsuleEnd(actor, yaw, -0x280, -0x40, q);
    hitListSubmitCapsule(p, 0x23, q, 0x10, machine.hitBox,
                         owner | kHitNoLockOn | hitGroup(kHitGroupBodies) | hitCapsule(3) | hitHeightLevel(0), kBodyAttack);
    capsuleEnd(actor, wrap16(yaw + 0x100), 0x200, 0, p);
    capsuleEnd(actor, wrap16(yaw + 0x380), 0x80, 0, q);
    hitListSubmitCapsule(p, 0x23, q, 0x20, machine.hitBox,
                         owner | kHitNoLockOn | hitGroup(kHitGroupBodies) | hitCapsule(1) | hitHeightLevel(0), kBodyAttack);
    capsuleEnd(actor, wrap16(yaw - 0x100), 0x200, 0, p);
    capsuleEnd(actor, wrap16(yaw - 0x380), 0x80, 0, q);
    hitListSubmitCapsule(p, 0x23, q, 0x20, machine.hitBox,
                         owner | kHitNoLockOn | hitGroup(kHitGroupBodies) | hitCapsule(1) | hitHeightLevel(0), kBodyAttack);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}


// [0x8cf9ec]
enum class St19TargetState : uint8_t { StillInit = 0, Still = 1, TargetInit = 2, Target = 3, Gone = 4 };
// [0x8cfa00]
enum class St19TargetSub : uint8_t { Regenerate = 0, Fall = 1 };
enum class JoltStep : uint8_t { Start = 0, Shaking = 1, Falling = 2 };

const DataTable<CollisionBoxRecord> st19StillBox{TableId::St15PropsSt19StillBox};
const DataTable<InteractionBoxRecord> st19StillTalkBox{TableId::St15PropsSt19StillTalkBox};
constexpr int16_t kRegenTicks = 0x3c;
constexpr uint8_t kJoltTicks = 0x59;
// the jolted target's place (16.16; the toward-the-camera jolt)
constexpr int32_t kJoltX = int32_t(0xfc000000u), kJoltY = int32_t(0xff000000u), kJoltZ = int32_t(0xfa000000u);

// the jolt of 0x5afbd0 (toward the camera's yaw) / 0x5afcf0: a shake of +-0x20 along x for its ticks
void jolt(StaticActor& actor, St19TargetFields& target, bool towardCamera) {
    if (actor.step == 0) {
        actor.stepCounter = 3;
        actor.step = uint8_t(JoltStep::Shaking);
        actor.velocity.y = 0;
        actor.acceleration.y = 0xc0;
        actor.velocity.x = 0;
        target.joltTicks = kJoltTicks;
        if (towardCamera) {
            actor.pos.x = kJoltX;
            actor.pos.y = kJoltY;
            actor.pos.z = kJoltZ;
        }
    }
    if (towardCamera) actor.rotation.y = int16_t(uint16_t(cameraState().viewAngles.y) + 0x800u);
    integrateVelocity(actor, actor.velocity.x, 0, 0);
    if (towardCamera) actor.rotation.y = int16_t(target.homeYaw);
    const uint8_t b = actor.stepCounter;
    if (b < 5) {
        actor.velocity.x = 0x20;
        actor.stepCounter = uint8_t(b + 1);
        target.joltTicks -= 1u;
    } else if (b <= 8) {
        actor.velocity.x = -0x20;
        actor.stepCounter = uint8_t(b + 1);
        target.joltTicks -= 1u;
        if (actor.stepCounter >= 9) actor.stepCounter = 1;
    }
}

// exe 0x5afbd0: a hit point back every 0x3c ticks; the jolt (toward the camera) while asked
void targetRegenerate(StaticActor& actor, St19TargetFields& target) {
    if (target.regenTicks != 0) {
        target.regenTicks = int16_t(target.regenTicks - 1);
    } else {
        if (actor.hitPoints < actor.maxHitPoints) actor.hitPoints = int16_t(actor.hitPoints + 1);
        target.regenTicks = kRegenTicks;
    }
    if (!(target.jolt & kJoltAsked)) return;
    if (actor.step > uint8_t(JoltStep::Shaking)) return;
    jolt(actor, target, true);
    if (target.joltTicks != 0) return;
    actor.step = uint8_t(JoltStep::Start);
    target.jolt &= uint8_t(~kJoltAsked);
}

// exe 0x5afcf0: out of hit points: the jolt, then it falls while above y 0
void targetFall(StaticActor& actor, St19TargetFields& target) {
    const JoltStep s = JoltStep(actor.step);
    if (s == JoltStep::Falling) {
        if (actor.pos.y.whole >= 0) return;
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        return;
    }
    if (uint8_t(s) > uint8_t(JoltStep::Falling)) return;
    jolt(actor, target, false);
    if (target.joltTicks != 0) return;
    actor.step += 1u;
}

// out of hit points: the fall
void targetDown(StaticActor& actor) {
    actor.hitPoints = -1;
    actor.subState = uint8_t(St19TargetSub::Fall);
    actor.step = 0;
}

// exe 0x5afa50: the hits fill the damage meter (its hit points kept): each overflow costs a hit point (a strong hit:
// every 4, with a jolt; else one, the meter back by 0x4f); the sub-state; the body's capsule
void st19Target(StaticActor& actor, St19TargetFields& target) {
    const uint32_t statRow = statTableRow(actorStatTables().statics, actor.type);
    const uint32_t v = target.hitBox.result;
    if (v & kHitAttackByPlayer) {
        const uint16_t hp0 = uint16_t(actor.hitPoints);
        hitPointsAdd(actor, -int(v & kHitDamageMask)); // exe 0x5e52f0
        target.damageMeter = int8_t(uint8_t(target.damageMeter) + uint8_t(uint8_t(actor.hitPoints) - uint8_t(hp0)));
        actor.hitPoints = int16_t(hp0);
        if (v & kHitAttackThrown) {
            target.jolt |= kJoltAsked;
            actor.step = 0;
            while (target.damageMeter < 0) {
                actor.hitPoints = int16_t(actor.hitPoints - 1);
                target.damageMeter = int8_t(uint8_t(target.damageMeter) + 4u);
                if (actor.hitPoints < 0) targetDown(actor);
            }
        } else if (target.damageMeter < 0) {
            actor.hitPoints = int16_t(hp0 - 1u);
            target.damageMeter = int8_t(uint8_t(target.damageMeter) + 0x4fu);
            if (actor.hitPoints < 0) targetDown(actor);
        }
    }
    switch (St19TargetSub(actor.subState)) {
    case St19TargetSub::Regenerate: targetRegenerate(actor, target); break;
    case St19TargetSub::Fall: targetFall(actor, target); break;
    default: notPortedAt(kStoryPort, "static 0x5af870 sub-state", actor.subState);
    }
    target.hitBox.previousX = actor.prevPos.x.whole;
    target.hitBox.previousZ = actor.prevPos.z.whole;
    target.hitBox.result = 0;
    const int16_t p[3] = {actor.pos.x.whole, wrap16(actor.pos.y.whole - 0x40), actor.pos.z.whole};
    const int16_t q[3] = {actor.pos.x.whole, wrap16(actor.pos.y.whole - 0x180), actor.pos.z.whole};
    const StaticActorStats row = staticStatsAt(statRow);
    hitListSubmitCapsule(p, 0x18, q, 0x4b, target.hitBox,
                         hitOwner(actor.slot) | hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupBodies) | hitCapsule(1),
                         row.damage | kHitAttackHarmless | kHitAttackSolid); // exe 0x5e4190
}

// exe 0x5af870
void st19TargetClass(const PlayerEnv& env, StaticActor& actor) {
    auto&& target = classState<St19TargetFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    actor.wasOnPlatform = actor.onPlatform;
    switch (St19TargetState(actor.state)) {
    case St19TargetState::StillInit: // exe 0x5af8e0
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.hitPoints = 1;
        actor.maxHitPoints = 1;
        actor.velocity.clearWords();
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.collisionBox.pointAt(st19StillBox.stateAddress());
        actor.interactionBox.pointAt(st19StillTalkBox.stateAddress());
        actor.interaction = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.lastMover = nullptr;
        actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
        actor.enterState(uint8_t(St19TargetState::Still));
        break;
    case St19TargetState::Still: break; // exe 0x52ef00
    case St19TargetState::TargetInit: { // exe 0x5af9c0
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.wallHit = 0;
        actor.actorHit = 0;
        const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints << 1) : int16_t(0);
        actor.maxHitPoints = hp;
        actor.hitPoints = hp;
        target.damageMeter = 0;
        target.regenTicksLow = uint8_t(kRegenTicks);
        target.regenTicksHigh = 0;
        target.hitBox.result = 0;
        target.homeYaw = uint16_t(actor.rotation.y);
        actor.state += 1u;
        break;
    }
    case St19TargetState::Target: st19Target(actor, target); break;
    case St19TargetState::Gone: freeActor(actor); break; // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x5af870 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
    // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow (render output)
}

} // namespace

void st15StillModelUpdate(const PlayerEnv&, uint32_t actor) { st15Still(staticActorAt(actor)); }
void patrolDroneUpdate(const PlayerEnv& env, uint32_t actor) { patrolDrone(env, staticActorAt(actor)); }
void st19CutsceneDroneUpdate(const PlayerEnv& env, uint32_t actor) { cutsceneDrone(env, staticActorAt(actor)); }
void hoverMachineUpdate(const PlayerEnv&, uint32_t actor) { hoverMachine(staticActorAt(actor)); }
void st19TargetUpdate(const PlayerEnv& env, uint32_t actor) { st19TargetClass(env, staticActorAt(actor)); }

} // namespace od::port
