// The ST54 swinging model (static, exe 0x5b7e90). Names are hypotheses from the observed behaviour. The state table is
// read from the exe's data and dispatched by the routine's address; an entry past the class's own states (the exe's
// tables run into the next class's tables) throws notPortedAt, as the other ports.
//
// States [0x8d0b3c]: 0 bind 0x5b7ee0 (model by the variant, pitched -0x60), 1 swing 0x5b7f60 (by its sub-state, set from
// outside: pitch toward 0x100, rolls +-0xc0, moves -0x50 along its rotation), 2 freed.
#include "content/late_game/st54_props.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

// the class's state routines (exe table 0x8d0b3c, by the state)
const DataTable<RoutineTable<3>> swingStateRoutines{TableId::St54PropsSwingStateRoutines};
// the swing's phases (the sub-state; 0 = hanging still)
enum class SwingPhase : uint8_t { Still = 0, Start = 1, RollRight = 2, Hold = 3, RollLeft = 4 };
constexpr int16_t kRollLimit = 0xc0, kRollStep = 0x40;
constexpr uint8_t kHoldTicks = 5;
constexpr int16_t kSwingPitch = 0x100;

// exe 0x5b7ee0
void bind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(uint32_t(staticStats(actor.type, 0).hitPoints) << 1) : 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.subState = 0;
    actor.clearStepBytes();
    ++actor.state;
    actor.rotation.x = int16_t(0xffa0);
    actor.rotation.z = 0;
}
// exe 0x5b7f60: the roll swung between +-0xc0 (a pause of 5 ticks on the right), the pitch eased toward 0x100, moved
// along its rotation
void swing(StaticActor& actor) {
    const uint8_t phase = actor.subState;
    switch (SwingPhase(phase)) {
    case SwingPhase::Start: actor.subState = uint8_t(phase + 1u); break;
    case SwingPhase::RollRight:
        actor.rotation.z = int16_t(actor.rotation.z + kRollStep);
        if (actor.rotation.z >= kRollLimit) {
            actor.subState = uint8_t(phase + 1u);
            actor.step = kHoldTicks;
        }
        break;
    case SwingPhase::Hold: {
        const uint8_t t = uint8_t(actor.step - 1);
        actor.step = t;
        if (t == 0) actor.subState = uint8_t(phase + 1u);
        break;
    }
    case SwingPhase::RollLeft:
        actor.rotation.z = int16_t(actor.rotation.z - kRollStep);
        if (actor.rotation.z < -kRollLimit) {
            actor.subState = uint8_t(SwingPhase::Start);
            actor.step = kHoldTicks;
        }
        break;
    default: break;
    }
    if (SwingPhase(actor.subState) == SwingPhase::Still) return;
    const int16_t p = actor.rotation.x;
    if (p < kSwingPitch) actor.rotation.x = int16_t(p + 4);
    else if (p > kSwingPitch) actor.rotation.x = int16_t(p - 4);
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, -0x50); // exe 0x5e3810
}
// exe 0x5b7e90
void swingingModel(StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (swingStateRoutines->idAt(actor.state)) {
    case RoutineId::St54BindRoutine: bind(actor); return;
    case RoutineId::St54SwingRoutine: swing(actor); return;
    case RoutineId::StaticFreedState: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "static 0x5b7e90 state", actor.state);
    }
}

} // namespace

void swingingModelUpdate(const PlayerEnv&, uint32_t actor) { swingingModel(staticActorAt(actor)); }

} // namespace od::port
