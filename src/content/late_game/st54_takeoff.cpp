// The ST54 take-off arc (pool 0x20 ST54 type 0x72 sub 1, exe 0x50dfc0). Names are hypotheses from the observed
// behaviour.
//
// States [0x8984a8]: 0 bind 0x50e060 (no box), 1 fly 0x50e0d0 (subs [0x8984b4]: 0 0x50e0f0; the entries past it are
// another class's), 2 freed; the animation (key = the spawn's first parameter) without the +0 & 1 test.
#include "content/late_game/st54_takeoff.hpp"
#include "content/late_game/st54_takeoff_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace st54_takeoff_state;

namespace {

constexpr const char* kActorsPort = "st54 takeoff port"; // the area of its port-gap errors

enum class TakeoffState : uint8_t { Bind = 0, Fly = 1, Free = 2 };
enum class TakeoffStep : uint8_t { Level = 0, Climb = 1 };
constexpr uint8_t kTakeoffAnim = 1;
constexpr int16_t kTopArc = 0x3c0;


// exe 0x50e060
void bind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.anim = kTakeoffAnim;
    actor.flags |= uint8_t(kActorScenery | kActorDrawn);
    actor.prevAnim = 0xff;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.enterStateBytes(uint8_t(TakeoffState::Fly));
    actor.step = 0;
}
// exe 0x50e0f0: level until the step is set from outside, then off along the arc: the pitch = cos(arc), the arc
// opening to 0x3c0
void fly(AnimatedActor& actor, TakeoffFields& takeoff) {
    if (TakeoffStep(actor.step) == TakeoffStep::Level) {
        actor.rotation.x = 0;
        actor.rotation.y = 0x400;
        takeoff.arcAngle = 0;
        return;
    }
    if (TakeoffStep(actor.step) != TakeoffStep::Climb) return;
    actor.rotation.x = fixedCos(takeoff.arcAngle);
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, -0x180); // exe 0x5e3810
    const int16_t arc = takeoff.arcAngle;
    if (arc >= kTopArc) {
        takeoff.arcAngle = kTopArc;
        return;
    }
    takeoff.arcAngle = int16_t(arc + (takeoff.fastClimb == 0 ? 3 : 4));
}
// exe 0x50dfc0
void takeoffArc(AnimatedActor& actor) {
    TakeoffFields& takeoff = classState<TakeoffFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (TakeoffState(actor.state)) { // [0x8984a8]
    case TakeoffState::Bind: bind(actor); break;
    case TakeoffState::Fly: // 0x50e0d0: [0x8984b4 + sub * 4]
        if (actor.subState != 0)
            notPortedAt(kActorsPort, "0x50e0d0 sub (index 1.. are another class's entries) [0x8984b4]", actor.subState);
        fly(actor, takeoff);
        break;
    case TakeoffState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kActorsPort, "0x50dfc0 state past [0x8984a8]", actor.state);
    }
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1ea0
        animPose(actor); // exe 0x5e2d90
        return;
    }
    animSet(actor, actor.anim, takeoff.startKey); // exe 0x5e1c50
    animPose(actor);
}

} // namespace

void takeoffArcUpdate(const PlayerEnv&, uint32_t actor) { takeoffArc(animatedActorAt(actor)); }

} // namespace od::port
