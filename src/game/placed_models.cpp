// The models put down by the player (see placed_models.hpp).
#include "game/actor_state.hpp"
#include "game/placed_models.hpp"
#include "game/placed_models_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace placed_models_state;

namespace {

inline constexpr const char* kPlacedArea = "placed models";

// [0x8c41f8] / [0x8c439c]
enum class PlacedState : uint8_t {
    Place = 0, // exe 0x5631a0 / 0x564d80: the model bound (else retried), placed
    Run = 1,   // exe 0x563280 / 0x564e50
    Free = 2,  // exe 0x5afdd0
};
// the placed model's sub-state (+9, set from outside): still, or stepped one way / the other
enum class PlacedSub : uint8_t { Still = 0, StepForward = 1, StepBack = 2 };
// the turning model's sub-state (+9, set from outside): still, or turning
enum class TurningSub : uint8_t { Still = 0, Turning = 1 };
constexpr uint8_t kPlacedReversed = 0x01; // the variant's bit 0: placed / stepped the other way


// the quadrant steps (exe data 0x8c41f0 / 0x8c43a8, the same bytes): quadrant q steps by ([q], [q + 1])
// in x / z -- one byte table read a quarter apart
const DataTable<ScalarTable<int8_t, 5>> placedSteps{TableId::PlacedModelsSteps};
const DataTable<ScalarTable<int8_t, 5>> turningSteps{TableId::PlacedModelsTurningSteps};

constexpr int kPlaceDistance = 0xa0;   // the placed model's distance from the player
constexpr int kTurningDistance = 0xd0; // the turning model's
constexpr uint16_t kTurningDrop = 0xff20; // the turning model's height above the player (-0xe0)
constexpr int kStepForward = 0xc, kStepsForward = 0xc, kStepBack = 0x10, kStepsBack = 9;
constexpr uint32_t kQuadrantRounding = 0x200; // the step's quadrant: the player's yaw rounded to the nearest
constexpr int16_t kTurnSpeed = 0x40;
constexpr uint16_t kTurnSound = 0x80;

// the quadrant of the player's yaw (+ add)
uint32_t yawQuadrant(uint32_t add) { return ((uint32_t(uint16_t(player().rotation.y)) + add) >> 10) & 3; }
// the model facing the player: the player's yaw turned half a turn
int16_t facingPlayer() { return int16_t((uint32_t(uint16_t(player().rotation.y)) - uint32_t(kHalfTurn)) & kAngleMask); }

// exe 0x5631a0: the model bound (else stays in Place); moved kPlaceDistance along the quadrant (variant bit 0:
// backwards), facing the player, still, the previous pose copied
void placedStart(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        actor.state = uint8_t(PlacedState::Place);
        return;
    }
    const uint32_t q = yawQuadrant(0);
    const int dx = placedSteps[q], dz = placedSteps[q + 1];
    const int k = (actor.variant & kPlacedReversed) ? -kPlaceDistance : kPlaceDistance;
    actor.pos.x.whole += uint32_t(dx * k);
    actor.pos.z.whole += uint32_t(dz * k);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.rotation.y = facingPlayer();
    actor.velocity.x = 0;
    actor.prevRotation = actor.rotation;
    actor.prevPos = actor.pos;
    actor.state += 1u;
}
// exe 0x563280: still; stepped (sub-state set from outside): once (step) the speed of kStepForward / kStepBack (its
// sign by the variant's bit 0) along the player's rounded quadrant and the target kStepsForward / kStepsBack steps away;
// then x / z step until the target is reached
void placedRun(StaticActor& actor) {
    PlacedModelFields& f = classState<PlacedModelFields>(actor);
    const uint32_t q = yawQuadrant(kQuadrantRounding);
    const PlacedSub sub = PlacedSub(actor.subState);
    if (sub == PlacedSub::Still) {
        actor.velocity.x = 0;
        actor.velocity.z = 0;
    } else if ((sub == PlacedSub::StepForward || sub == PlacedSub::StepBack) && actor.step == 0) {
        const bool reversed = (actor.variant & kPlacedReversed) != 0;
        const int k = sub == PlacedSub::StepForward ? (reversed ? -kStepForward : kStepForward)
                                                    : (reversed ? kStepBack : -kStepBack);
        const int n = sub == PlacedSub::StepForward ? kStepsForward : kStepsBack;
        const int16_t vx = wrap16(placedSteps[q] * k), vz = wrap16(placedSteps[q + 1] * k);
        actor.velocity.x = vx;
        actor.velocity.z = vz;
        f.targetX = int16_t(uint16_t(uint32_t(wrap16(vx * n)) + uint32_t(uint16_t(actor.pos.x.whole))));
        f.targetZ = int16_t(uint16_t(uint32_t(wrap16(vz * n)) + uint32_t(uint16_t(actor.pos.z.whole))));
        actor.step += 1u;
    }
    // toward the target, snapped when reached
    auto stepToward = [](int16_t& speed, const int16_t& target, int16_t& pos) {
        const int16_t v = speed;
        if (v == 0) return;
        const int16_t t = target, p = pos;
        if ((v > 0 && t > p) || (v < 0 && t < p)) {
            pos = int16_t(p + v);
            return;
        }
        pos = t;
        speed = 0;
    };
    stepToward(actor.velocity.x, f.targetX, actor.pos.x.whole);
    stepToward(actor.velocity.z, f.targetZ, actor.pos.z.whole);
}

// exe 0x564d80: the model bound (else stays in Place); kTurningDistance beside the player along the quadrant, raised,
// facing the player, the previous pose copied
void turningStart(StaticActor& actor) {
    TurningModelFields& f = classState<TurningModelFields>(actor);
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        actor.state = uint8_t(PlacedState::Place);
        return;
    }
    const uint32_t q = yawQuadrant(0);
    const int dx = turningSteps[q], dz = turningSteps[q + 1];
    actor.pos.y.whole += uint32_t(kTurningDrop);
    actor.pos.z.whole += uint32_t(dx * kTurningDistance);
    actor.pos.x.whole -= uint32_t(dz * kTurningDistance);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    f.startCleared = 0;
    actor.enterState(uint8_t(PlacedState::Run));
    actor.rotation.y = facingPlayer();
    actor.prevRotation = actor.rotation;
    f.startYaw = actor.rotation.y;
    actor.prevPos = actor.pos;
}
// exe 0x564e50: turning (sub-state 1, set from outside): the sound once, the pitch turned kTurnSpeed a tick
void turningRun(StaticActor& actor) {
    if (TurningSub(actor.subState) != TurningSub::Turning) return;
    if (actor.step == 0) {
        soundPlayAtPos(kTurnSound, kSoundAtListener); // exe 0x5d5380
        actor.step += 1u;
    }
    actor.rotation.x -= uint32_t(kTurnSpeed);
}

} // namespace

void placedModelUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    switch (PlacedState(actor.state)) {
    case PlacedState::Place: placedStart(actor); return;
    case PlacedState::Run: placedRun(actor); return;
    case PlacedState::Free: freeActor(actor); return;
    default: notPortedAt(kPlacedArea, "0x563180 state", actor.state);
    }
}

void turningModelUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    switch (PlacedState(actor.state)) {
    case PlacedState::Place: turningStart(actor); return;
    case PlacedState::Run: turningRun(actor); return;
    case PlacedState::Free: freeActor(actor); return;
    default: notPortedAt(kPlacedArea, "0x564d60 state", actor.state);
    }
}

} // namespace od::port
