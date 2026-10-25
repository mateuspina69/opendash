// The floor switches of the Nino ruin (static type 1 of their stage tables), one routine per exe routine; the names are
// hypotheses from the observed behaviour.
//   0x590ba0  ST36: variant 0 the switch -- ridden by the player it sinks (1 a tick, 8 below its home height; sound
//             0x1e2 when stepped on), else it rises back (2 a tick). Variants 1 / 2: the gates, driven by their
//             parameter (the room's logic sets it): 0 they sink 1 a tick to their home height, 1 they rise 0x30 a tick
//             to y 0. A stand-on solid every tick.
//   0x590cd0  ST37: the pressure plates -- the same sinking under the player or the room's box (the room scripts'
//             actor 2), their flag 0x96 / 0x97 set while pressed. Their init is the switch's (exe 0x590cf0).
#include "content/nino/floor_switch.hpp"
#include "content/nino/floor_switch_state.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/field_alias.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace floor_switch_state;

namespace {

enum class SwitchState : uint8_t { Init = 0, Run = 1, Idle2 = 2, Idle3 = 3 };
enum class SwitchVariant : uint8_t { Switch = 0, GateA = 1, GateB = 2 };
// The switch's parameter: ridden by the player (the switch) / raised (the gates)
struct SwitchParams {
    explicit SwitchParams(const StaticActor& actor) : SwitchParams(const_cast<StaticActor&>(actor)) {}
    explicit SwitchParams(StaticActor& actor) : pressed(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> pressed;
};
constexpr uint16_t kStepSound = 0x1e2;
constexpr int kSinkDepth = 8;
enum class PlateKind : int8_t { First = 0, Second = 1 };
// The plate's parameters: pressed (written every tick) and its kind (its flag)
struct PlateParams {
    explicit PlateParams(const StaticActor& actor) : PlateParams(const_cast<StaticActor&>(actor)) {}
    explicit PlateParams(StaticActor& actor) : pressed(actor.params[0]), kind(actor.params[3]) {}
    uint8_t& pressed;
    FieldAlias<int8_t, uint8_t> kind;
};
constexpr uint32_t kFlagFirstPlate = 0x96, kFlagSecondPlate = 0x97;
constexpr size_t kRoomBoxActor = 2; // the room scripts' actor 2: the box the player carries onto the plates

// exe 0x590cf0: the model of its variant, its home height, no motion
void switchInit(StaticActor& actor, SwitchFields& fields) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(a, variant)
        freeActor(actor);                        // exe 0x5e1540
        return;
    }
    fields.homeY = actor.pos.y.whole;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.velocity.clearWords();
    actor.enterState(uint8_t(SwitchState::Run));
    actor.prevPos = actor.pos;
}
// exe 0x590bc0
void switchRun(StaticActor& actor, SwitchFields& fields) {
    SwitchParams params(actor);
    if (SwitchVariant(actor.variant) == SwitchVariant::Switch) {
        actor.prevPos = actor.pos;
        params.pressed = playerRides(actor) ? 1 : 0; // exe 0x59beb0
        if (params.pressed == 1) {
            if (actor.subState == 0) {
                soundPlayAtPos(kStepSound, actor.pos);
                actor.subState = 1;
            }
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 1u);
            const int16_t home = fields.homeY;
            if (int32_t(actor.pos.y.whole) > int32_t(home) + kSinkDepth)
                actor.pos.y.whole = int16_t(uint16_t(uint32_t(uint16_t(home)) + kSinkDepth));
        } else if (params.pressed == 0) {
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 2u);
            actor.subState = 0;
            if (actor.pos.y.whole < fields.homeY) actor.pos.y.whole = fields.homeY;
        }
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
        return;
    }
    if (actor.variant > uint8_t(SwitchVariant::GateB)) return;
    const int8_t raised = params.pressed;
    if (raised == 0) {
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 1u);
        if (actor.pos.y.whole < fields.homeY) actor.pos.y.whole = fields.homeY;
    } else if (raised == 1) {
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0x30u);
        if (actor.pos.y.whole > 0) actor.pos.y.whole = 0;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}
// exe 0x590ee0: the player, or the room's box, stands on the plate
bool platePressed(const StaticActor& plate) {
    if (playerRides(plate)) return true; // exe 0x59beb0 inlined
    const Actor& box = actorAt(roomScriptState().actors[kRoomBoxActor]);
    return box.onPlatform != 0 && ((slotIdentityWord(box.platform) ^ slotIdentityWord(plate)) & 0x1fffff) == 0;
}
// exe 0x590d50
void plateRun(StaticActor& plate, SwitchFields& fields) {
    PlateParams params(plate);
    const PlateKind kind = PlateKind(params.kind.get());
    if (kind != PlateKind::First && kind != PlateKind::Second) return;
    const uint32_t flag = kind == PlateKind::First ? kFlagFirstPlate : kFlagSecondPlate;
    const bool pressed = platePressed(plate);
    params.pressed = pressed ? 1 : 0;
    if (pressed) {
        if (plate.subState == 0) {
            soundPlayAtPos(kStepSound, plate.pos);
            plate.subState = 1;
        }
        plate.pos.y.whole = int16_t(uint16_t(plate.pos.y.whole) + 1u);
        if (int32_t(plate.pos.y.whole) > int32_t(fields.homeY) + kSinkDepth)
            plate.pos.y.whole = int16_t(uint16_t(uint32_t(uint16_t(fields.homeY)) + kSinkDepth));
        setStoryFlag(flag);
    } else {
        if (kind == PlateKind::First) plate.prevPos = plate.pos;
        plate.subState = 0;
        plate.pos.y.whole = int16_t(uint16_t(plate.pos.y.whole) - 2u);
        if (plate.pos.y.whole < fields.homeY) plate.pos.y.whole = fields.homeY;
        clearStoryFlag(flag);
    }
    actorCollisionSubmitSolid(plate, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
}
// exe 0x590cd0
void pressurePlate(StaticActor& plate) {
    SwitchFields& fields = classState<SwitchFields>(plate);
    switch (SwitchState(plate.state)) {
    case SwitchState::Init: switchInit(plate, fields); return; // exe 0x590cf0
    case SwitchState::Run: plateRun(plate, fields); return;
    default: notPortedAt(kStoryPort, "0x590cd0 state (table 0x8cdd94)", plate.state);
    }
}
// exe 0x590ba0
void floorSwitch(StaticActor& actor) {
    SwitchFields& fields = classState<SwitchFields>(actor);
    switch (SwitchState(actor.state)) {
    case SwitchState::Init: switchInit(actor, fields); return;
    case SwitchState::Run: switchRun(actor, fields); return;
    case SwitchState::Idle2:
    case SwitchState::Idle3: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x590ba0 state (past the table)", actor.state);
    }
}

} // namespace

void floorSwitchUpdate(const PlayerEnv&, uint32_t actor) { floorSwitch(staticActorAt(actor)); }
void pressurePlateUpdate(const PlayerEnv&, uint32_t actor) { pressurePlate(staticActorAt(actor)); }

} // namespace od::port
