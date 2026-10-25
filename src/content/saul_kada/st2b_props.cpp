// The placed props of ST2B (static type 1, exe 0x5906a0) and the plates of ST32 (exe 0x590b80); hypothesis names, one
// routine per exe routine.
//
// exe 0x5906a0: states [0x8cdd48] 0 bind 0x5906e0, 1 the routine of the variant [0x8cdd54 + variant * 4]; variant 2 (a
// pot) runs the routines of its sub-states [0x8cdd60 + sub * 4] -- the two tables overlap (sub k = variant k + 3), so
// both dispatch on the routine the exe table holds. The pot: lifted, carried, thrown (gravity 0x40, at most 0x800)
// until it hits something, then broken (a blast) and freed.
// exe 0x590b80: states [0x8cdd74] 0 the in-place bind 0x5904f0, 4 / 8 the plates' bind 0x590cf0, 5 0x590bc0 (variant
// 0 sinks while the player stands on it, 1 / 2 move by their parameter), 9 0x590d50 (the floor switches, flags 0x96 /
// 0x97); the rest nothing.
#include "content/saul_kada/st2b_props.hpp"
#include "content/saul_kada/st2b_props_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st2b_props_state;

namespace {

// the props' routines (exe tables 0x8cdd54 by variant / 0x8cdd60 by the pot's sub-state: the named routines of
// engine/routines.csv)
const DataTable<RoutineTable<0x10>> variantRoutines{TableId::St2bPropVariantRoutines};
const DataTable<RoutineTable<0x10>> potRoutines{TableId::St2bPropPotRoutines};
enum class PropState : uint8_t { Bind = 0, Run = 1 };
enum class PotSub : uint8_t { Wait = 0, Rest = 1, Carried = 2, Flight = 3, Break = 4 };
enum class PropVariant : uint8_t { Turned = 1, Pot = 2 };
enum class SwitchVariant : uint8_t { Switch = 0, GateA = 1, GateB = 2 };

// The props' parameters: a plate's / a switch's pressed (or raised) byte, the floor switch's kind
struct PropParams {
    explicit PropParams(const StaticActor& actor) : PropParams(const_cast<StaticActor&>(actor)) {}
    explicit PropParams(StaticActor& actor) : pressed(actor.params[0]), plateKind(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> pressed;
    FieldAlias<int8_t, uint8_t> plateKind;
};
constexpr uint8_t kPotHeld = 0x01;   // no hit entry
constexpr uint8_t kPotThrown = 0x02; // a mover hurting what it hits
constexpr uint8_t kPotThrowHurt = 0x04;
constexpr uint32_t kFlagTurnedProp = 0x581;
constexpr int16_t kTurnedRoll = 0xc00;
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e, kStepSound = 0x1e2;
constexpr int kPotHeight = 0x5a;
constexpr uint16_t kPotBlastSize = 0x100;
constexpr int16_t kPotGravity = 0x40, kPotMaxFall = 0x800;
constexpr int kSinkDepth = 8;
constexpr uint32_t kFlagFirstPlate = 0x96, kFlagSecondPlate = 0x97;
constexpr size_t kRoomBoxActor = 2; // the room scripts' actor 2: the box the player carries onto the plates
// the pot's hit entries: resting, a liftable body (radius 0x60); thrown, the player's attack
constexpr uint32_t kRestingShape = hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitRadius(0x60);
constexpr uint32_t kThrownShape = hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x60);
constexpr uint32_t kThrownAttack = kHitAttackHarmless | kHitAttackByPlayer;

// the props' hit points: their type's row (0 without one)
uint16_t propHitPoints(const StaticActor& actor) {
    return hasStaticStats(actor.type) ? uint16_t(staticStats(actor.type, 0).hitPoints) : uint16_t(0);
}
void solid(StaticActor& actor) { actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); } // exe 0x414050
// exe 0x590ee0: the player or the room's box stands on the plate
bool platePressed(const StaticActor& plate) {
    if (playerRides(plate)) return true; // exe 0x59beb0
    const Actor& box = actorAt(roomScriptState().actors[kRoomBoxActor]); // read unchecked, as the exe
    return box.onPlatform != 0 && ((slotIdentityWord(box.platform) ^ slotIdentityWord(plate)) & 0x1fffff) == 0;
}
void enterSub(StaticActor& actor, PotSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}

// the routine (gapPointer: its table entry, for a port gap's message)
void runRoutine(const PlayerEnv& env, StaticActor& actor, RoutineId routine, uint32_t gapPointer = 0);

// exe 0x5906e0
void bind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    const uint16_t hp = propHitPoints(actor);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::St2bPropsPropBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(PropState::Run));
    if (PropVariant(actor.variant) == PropVariant::Pot) { // the pot: its hit points again, drawn, its boxes
        const uint16_t potHp = propHitPoints(actor);
        actor.maxHitPoints = int16_t(potHp);
        actor.hitPoints = int16_t(potHp);
        actor.flags |= kActorDrawn;
        actor.collisionBox.pointAt(TableId::St2bPropsPropBox);
        actor.interactionBox.pointAt(TableId::St2bPropsPotLiftBox);
        actor.interaction = kInteractLift;
        actor.wasOnPlatform = actor.onPlatform;
        return;
    }
    if (PropVariant(actor.variant) == PropVariant::Turned && storyFlag(kFlagTurnedProp))
        actor.rotation.z = kTurnedRoll;
    actor.wasOnPlatform = actor.onPlatform;
}
// exe 0x590aa0: gravity until it hits something
void potFlight(const PlayerEnv& env, StaticActor& actor, PotFields& pot) {
    int16_t vy = int16_t(actor.velocity.y + kPotGravity);
    if (vy > kPotMaxFall) vy = kPotMaxFall;
    actor.velocity.y = vy;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    const uint32_t result = actorTestMove(env, actor);                // exe 0x40f570
    actor.wallHit = result;
    if (int32_t(result) < 0 || actor.actorHit != 0 || (pot.hitBox.result & (kHitAttackStrong | kHitAttackSolid)))
        enterSub(actor, PotSub::Break);
}
// exe 0x590800: the hits, the sub-state, the boxes (resting: solid; carried / thrown: a mover)
void potMain(const PlayerEnv& env, StaticActor& actor, PotFields& pot) {
    actor.tint = kTintNeutral;
    const uint32_t hit = pot.hitBox.result, damage = hit & kHitDamageMask;
    if (damage && (hit & (kHitAttackStrong | kHitAttackByPlayer))) {
        actor.tint = kTintFlash;
        soundPlayAtPos((hit & kHitAttackStrongPush) ? kStrongHurtSound : kHurtSound, actor.pos); // exe 0x5d5380
        if (hitPointsAdd(actor, -int(damage))) enterSub(actor, PotSub::Break);              // exe 0x5e52f0
    }
    runRoutine(env, actor, potRoutines->idAt(actor.subState), potRoutines->pointerAt(actor.subState));
    if (!(actor.flags & kActorLive)) return;
    actor.wasOnPlatform = actor.onPlatform;
    pot.hitBox.result = 0;
    pot.hitBox.previousX = actor.pos.x.whole;
    pot.hitBox.previousZ = actor.pos.z.whole;
    const uint8_t flags = pot.potFlags;
    if (flags & kPotHeld) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - kPotHeight), actor.pos.z.whole};
    if (!(flags & kPotThrown)) {
        hitListSubmit(pos, pot.hitBox, hitOwner(actor.slot) | kRestingShape,
                      uint32_t((flags & kPotThrowHurt) | 0x41) << 17); // exe 0x5e4120: harmless, solid (thrown hurt)
        solid(actor);
        return;
    }
    hitListSubmit(pos, pot.hitBox, hitOwner(actor.slot) | kThrownShape,
                  uint32_t(staticStats(actor.type, 0).damage) | kThrownAttack);
    actorCollisionSubmitMover(actor); // exe 0x4140b0 (the shadow exe 0x5f20f0: render)
}
// exe 0x590970: waiting to be lifted
void potRest(StaticActor& actor, PotFields& pot) {
    if (actor.step == 0) {
        pot.potFlags = 0;
        actor.enterStep(1);
    }
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x5909a0 -> 0x573a60
        actor.subState = uint8_t(PotSub::Carried);
        pot.potFlags |= kPotHeld;
        actor.onPlatform = 0;
    }
}
// exe 0x5909d0: at the player's hands until thrown (along his aim) / dropped
void potCarried(const PlayerEnv& env, StaticActor& actor, PotFields& pot) {
    if (actor.step == 0) {
        pot.potFlags = kPotHeld;
        actor.enterStep(1);
    }
    carryAtHands(actor, 0, -0x10, 0);             // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    if (release == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x300, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        pot.potFlags = kPotThrown;
    } else {
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
        pot.potFlags = 0;
    }
    actor.acceleration.y = kPotGravity;
    actor.rotation.y = player().rotation.y;
    pot.rotationSpare = player().rotation.y;
    actor.airborne = 1;
    actor.actorHit = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    enterSub(actor, PotSub::Flight);
    potFlight(env, actor, pot);
}
// exe 0x590b00: broken -- a blast and freed
void potBreak(StaticActor& actor) {
    if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(slot);
        blast.subtype = 0;
        blast.variant = 0;
        blast.loopFlags |= kActorSweepGroup2;
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.pos = actor.pos;
        blast.pos.y.whole = int16_t(actor.pos.y.whole - kPotHeight);
        BlastParams params(actorHeaderAt(slot));
        params.size = kPotBlastSize;
        params.attack = staticStats(actor.type, 0).power;
    }
    actorDetachRiders(actor); // exe 0x414e70
    freeActor(actor);
}
// exe 0x5904f0: re-bound in place, its motion cleared, the next state
void rebindInPlace(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    PotFields& bytes = classState<PotFields>(actor);
    bytes.hitBox.result = 0;
    bytes.word = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.acceleration.setXY(0, 0);
    actor.prevPos = actor.pos;
    actor.acceleration.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.collisionBox = nullptr;
    actor.lastMover = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.prevRotation = actor.rotation;
    ++actor.state;
}
// exe 0x590cf0: the plates' bind (their home height)
void plateBind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    classState<PlateFields>(actor).homeY = actor.pos.y.whole;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.velocity.clearWords();
    actor.enterState(uint8_t(PropState::Run));
    actor.prevPos = actor.pos;
}
// the plate pressed: down 1 a tick to 8 below its home height (the step sound when it starts)
void sink(StaticActor& actor, const PlateFields& plate) {
    if (actor.subState == 0) {
        soundPlayAtPos(kStepSound, actor.pos);
        actor.subState = 1;
    }
    actor.pos.y.whole += 1u;
    if (actor.pos.y.whole > plate.homeY + kSinkDepth) actor.pos.y.whole = int16_t(plate.homeY + kSinkDepth);
}
// exe 0x590bc0: the switch sinks while the player stands on it (variant 0); the gates move by their parameter (1 / 2)
void switchRun(StaticActor& actor) {
    PlateFields& plate = classState<PlateFields>(actor);
    PropParams params(actor);
    const SwitchVariant variant = SwitchVariant(actor.variant);
    if (variant == SwitchVariant::Switch) {
        actor.prevPos = actor.pos;
        const bool on = playerRides(actor); // exe 0x59beb0
        params.pressed = on ? 1 : 0;
        if (on) {
            sink(actor, plate);
        } else {
            actor.pos.y.whole -= 2u;
            actor.subState = 0;
            if (actor.pos.y.whole < plate.homeY) actor.pos.y.whole = plate.homeY;
        }
        solid(actor);
        return;
    }
    if (actor.variant > uint8_t(SwitchVariant::GateB)) return;
    switch (params.pressed) {
    case 0:
        actor.pos.y.whole -= 1u;
        if (actor.pos.y.whole < plate.homeY) actor.pos.y.whole = plate.homeY;
        break;
    case 1:
        actor.pos.y.whole += 0x30u;
        if (actor.pos.y.whole > 0) {
            actor.pos.y.whole = 0;
            solid(actor);
            return;
        }
        break;
    default: break;
    }
    solid(actor);
}
// one half of exe 0x590d50: the floor switch pressed by the player or the room's box sets its flag
void floorSwitch(StaticActor& actor, uint32_t flag, bool clearPrevious) {
    PlateFields& plate = classState<PlateFields>(actor);
    const bool on = platePressed(actor);
    PropParams(actor).pressed = on ? 1 : 0;
    if (on) {
        sink(actor, plate);
        setStoryFlag(flag);
    } else {
        if (clearPrevious) actor.prevPos = actor.pos;
        actor.subState = 0;
        actor.pos.y.whole -= 2u;
        if (actor.pos.y.whole < plate.homeY) actor.pos.y.whole = plate.homeY;
        clearStoryFlag(flag);
    }
    solid(actor);
}
// exe 0x590d50: the floor switches -- kind 1 flag 0x97, kind 0 flag 0x96
void plateRun(StaticActor& actor) {
    const int8_t kind = PropParams(actor).plateKind;
    if (kind == 1) floorSwitch(actor, kFlagSecondPlate, false);
    else if (kind == 0) floorSwitch(actor, kFlagFirstPlate, true);
}

void runRoutine(const PlayerEnv& env, StaticActor& actor, RoutineId routine, uint32_t gapPointer) {
    PotFields& pot = classState<PotFields>(actor);
    switch (routine) {
    case RoutineId::EmptyRoutine: return;
    case RoutineId::St2bPropPotMain: potMain(env, actor, pot); return;
    case RoutineId::St2bPropPotWait: enterSub(actor, PotSub::Rest); return; // exe 0x590960
    case RoutineId::St2bPropPotRest: potRest(actor, pot); return;
    case RoutineId::St2bPropPotCarried: potCarried(env, actor, pot); return;
    case RoutineId::St2bPropPotFlight: potFlight(env, actor, pot); return;
    case RoutineId::St2bPropPotBreak: potBreak(actor); return;
    case RoutineId::St2bPropRebindInPlace: rebindInPlace(actor); return;
    case RoutineId::St2bPropPlateBind: plateBind(actor); return;
    case RoutineId::St2bPropSwitchRun: switchRun(actor); return;
    case RoutineId::St2bPropPlateRun: plateRun(actor); return;
    default: notPortedAt(kStoryPort, "0x5906a0 handler", gapPointer);
    }
}

// exe 0x5906a0
void st2bProp(const PlayerEnv& env, StaticActor& actor) {
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.wasAirborne = actor.airborne;
    switch (PropState(actor.state)) {
    case PropState::Bind: bind(actor); return;
    case PropState::Run: runRoutine(env, actor, variantRoutines->idAt(actor.variant), variantRoutines->pointerAt(actor.variant)); return; // 0x5907e0
    default: notPortedAt(kStoryPort, "0x5906a0 state", actor.state);
    }
}
// exe 0x590b80 [0x8cdd74]
void st32Plate(const PlayerEnv& env, StaticActor& actor) {
    switch (actor.state) {
    case 0: runRoutine(env, actor, RoutineId::St2bPropRebindInPlace); return;
    case 4:
    case 8: runRoutine(env, actor, RoutineId::St2bPropPlateBind); return;
    case 5: runRoutine(env, actor, RoutineId::St2bPropSwitchRun); return;
    case 9: runRoutine(env, actor, RoutineId::St2bPropPlateRun); return;
    default:
        if (actor.state >= 12) notPortedAt(kStoryPort, "0x590b80 state (table 0x8cdd74)", actor.state);
        return;
    }
}

} // namespace

void st2bPropUpdate(const PlayerEnv& env, uint32_t actor) { st2bProp(env, staticActorAt(actor)); }
void st32PlateUpdate(const PlayerEnv& env, uint32_t actor) { st32Plate(env, staticActorAt(actor)); }

} // namespace od::port
