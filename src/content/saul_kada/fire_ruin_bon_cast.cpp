// Bon Bonne in the fire ruin's cutscenes (pool 0x20 type 0x51, story 13; hypothesis names), one routine per exe routine.
//
// exe 0x4dd5f0 (ST28 sub 5, script 0x42): animation 0x1d; variant 0 walks back (speed -0x100) between the step ticks 6
// and 0x4b of the step sub-id 1; variant 1 is lit (the flash tint) and freed at the step sub-id 1. States [0x89580c]:
// 0 bind 0x4dd670, 1 by variant 0x4dd6f0 [0x895818], 2 freed 0x468fa0, 3 / 4 the variants' routines.
// exe 0x4dd1e0 (ST27, scripts 0x40 / 0x41): animation 0x1d; by variant [0x8957fc] it follows the script's step sub-id
// and tick: turns (+-0x40 / 0x80 a tick in windows), the animation chains 0x1e..0x24 (each waiting for the animation's
// end), a walk (speed -0x100) from (-0x400, z 0) at the step sub-id 6, placed at (-0x400, z 0x300) at the step sub-id
// 1; variants 4..8 reuse exe 0x4dd5f0's routines. States [0x8957f0]: 0 bind 0x4dd290, 1 by variant 0x4dd300, 2 freed,
// 3.. the variants' routines.
#include "content/saul_kada/fire_ruin_bon_cast.hpp"
#include "content/saul_kada/fire_ruin_bon_cast_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace fire_ruin_bon_cast_state;

namespace {

constexpr uint8_t kAnimStand = 0x1d;
constexpr int16_t kWalkSpeed = int16_t(0xff00);

// the update head of both classes: the previous animation, the tint neutral, the previous pose and contact
void castHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
}

// ---- exe 0x4dd5f0 (ST28 script 0x42) ----------------------------------------------------------------------------------
// [0x89580c]
enum class RetreatState : uint8_t { Bind = 0, ByVariant = 1, Free = 2, WalkBack = 3, Flash = 4 };
enum class RetreatVariant : uint8_t { WalkBack = 0, Flash = 1 };
constexpr uint8_t kRetreatCue = 1;

// exe 0x4dd710: walks back between the step ticks 6 and 0x4b of the step sub-id 1
void walkBack(AnimatedActor& actor) {
    const ScriptState& script = scriptState();
    if (script.stepSub == kRetreatCue && script.stepTick > 5 && script.stepTick < 0x4c)
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    animStepPose(actor);                                  // exe 0x5e1e80
}
// exe 0x4dd750: lit, freed at the step sub-id 1
void flashAway(AnimatedActor& actor) {
    actor.tint = kTintFlash;
    if (scriptState().stepSub == kRetreatCue) actor.state = uint8_t(RetreatState::Free);
}
// exe 0x4dd670
void bindRetreat(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnimShown;
    animSetPose(actor, kAnimStand, 0); // exe 0x5e1c30
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = kWalkSpeed;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
    classState<BonCastFields>(actor).ticks = 0;
    actor.subState = 0;
    actor.restartStep();
    actor.state = uint8_t(RetreatState::ByVariant);
}
// exe 0x4dd6f0 [0x895818]
void retreatByVariant(AnimatedActor& actor) {
    switch (RetreatVariant(actor.variant)) {
    case RetreatVariant::WalkBack: walkBack(actor); return;
    case RetreatVariant::Flash: flashAway(actor); return;
    default: notPortedAt(kStoryPort, "0x4dd6f0 variant (table 0x895818)", actor.variant);
    }
}
void bonRetreatCast(AnimatedActor& actor) {
    castHead(actor);
    switch (RetreatState(actor.state)) {
    case RetreatState::Bind: bindRetreat(actor); return;
    case RetreatState::ByVariant: retreatByVariant(actor); return;
    case RetreatState::Free: freeActor(actor); return; // 0x468fa0
    case RetreatState::WalkBack: walkBack(actor); return;
    case RetreatState::Flash: flashAway(actor); return;
    default: notPortedAt(kStoryPort, "0x4dd5f0 state (table 0x89580c)", actor.state);
    }
}

// ---- exe 0x4dd1e0 (ST27 scripts 0x40 / 0x41) --------------------------------------------------------------------------
// [0x8957f0]: the variants' routines from state 3
enum class ScriptCastState : uint8_t { Bind = 0, ByVariant = 1, Free = 2, FirstRoutine = 3, LastRoutine = 11 };
// [0x8957fc]
enum class BonRoutine : uint8_t {
    Gestures = 0,      // exe 0x4dd320
    TurnAndWalk = 1,   // exe 0x4dd4e0
    Idle = 2,          // exe 0x52ef00
    Placed = 3,        // exe 0x4dd590
    Rebind = 4,        // exe 0x4dd670
    RetreatCast = 5,   // exe 0x4dd6f0
    Free = 6,          // exe 0x468fa0
    WalkBack = 7,      // exe 0x4dd710
    Flash = 8,         // exe 0x4dd750
    Count = 9,
};
constexpr int16_t kPlacedX = int16_t(0xfc00);

// exe 0x4dd320: the turn at the step sub-id 4, the animation chains of the step sub-ids 7 / 8 / 10
void gestures(AnimatedActor& actor) {
    BonCastFields& cast = classState<BonCastFields>(actor);
    const uint8_t cue = scriptState().stepSub;
    const int32_t tick = scriptState().stepTick;
    if (cue == 4) {
        if (tick > 5 && tick < 0x12) actor.rotation.y -= 0x80u;
    } else if (cue == 7) {
        switch (actor.subState) {
        case 0:
            if (tick == 5) {
                actor.anim = 0x1e;
                actor.subState = 1;
            }
            break;
        case 1:
            if (actor.animEnded()) {
                actor.anim = 0x1f;
                cast.ticks = 0;
                actor.subState = 2;
            }
            break;
        case 2:
            ++cast.ticks;
            if (cast.ticks > 0x64 && actor.animEnded()) {
                actor.anim = 0x20;
                cast.ticks = 0;
                actor.subState = 3;
            }
            break;
        case 3:
            if (actor.animEnded()) {
                actor.anim = 0x21;
                actor.subState = 0;
            }
            break;
        default: break;
        }
    } else if (cue == 8) {
        if (actor.subState == 0) {
            if (tick == 0xa) {
                actor.anim = 0x22;
                actor.subState = 1;
            }
        } else if (actor.subState == 1 && actor.animEnded()) {
            actor.anim = 0x23;
            actor.subState = 0;
        }
    } else if (cue == 10) {
        switch (actor.subState) {
        case 0:
            if (tick == 0xa) {
                actor.anim = 0x24;
                actor.subState = 1;
            }
            break;
        case 1:
            if (actor.animEnded()) {
                actor.anim = kAnimStand;
                actor.subState = 2;
            }
            break;
        case 2:
            if (tick > 0x280 && tick < 0x298) actor.rotation.y += 0x40u;
            break;
        default: break;
        }
    }
}
// exe 0x4dd4e0: turns at the step sub-ids 2 / 4, walks from (-0x400, z 0) at 6, freed at 7
void turnAndWalk(AnimatedActor& actor) {
    const uint8_t cue = scriptState().stepSub;
    const int32_t tick = scriptState().stepTick;
    if (cue == 2) {
        if (tick > 0x154 && tick < 0x15e) actor.rotation.y += 0x40u;
    } else if (cue == 4) {
        if (tick > 5 && tick < 0x12) actor.rotation.y += 0x40u;
    } else if (cue == 6) {
        if (tick == 1) {
            actor.pos.x.whole = kPlacedX;
            actor.pos.z.whole = 0;
            actor.rotation.y = 0x800;
            actor.velocity.z = kWalkSpeed;
        }
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    } else if (cue == 7) {
        actor.state = uint8_t(ScriptCastState::Free);
    }
}
// exe 0x4dd590: the turn at the step sub-id 0, placed at (-0x400, z 0x300) at 1
void placed(AnimatedActor& actor) {
    const uint8_t cue = scriptState().stepSub;
    const int32_t tick = scriptState().stepTick;
    if (cue == 0) {
        if (tick > 0xa && tick < 0x16) actor.rotation.y -= 0x80u;
    } else if (cue == 1 && actor.subState == 0 && tick == 1) {
        actor.pos.x.whole = kPlacedX;
        actor.pos.z.whole = 0x300;
        actor.rotation.y = 0x800;
        actor.subState = 1;
    }
}
void runRoutine(AnimatedActor& actor, BonRoutine routine) {
    switch (routine) {
    case BonRoutine::Gestures: gestures(actor); return;
    case BonRoutine::TurnAndWalk: turnAndWalk(actor); return;
    case BonRoutine::Idle: return;
    case BonRoutine::Placed: placed(actor); return;
    case BonRoutine::Rebind: bindRetreat(actor); return;
    case BonRoutine::RetreatCast: retreatByVariant(actor); return;
    case BonRoutine::Free: freeActor(actor); return;
    case BonRoutine::WalkBack: walkBack(actor); return;
    case BonRoutine::Flash: flashAway(actor); return;
    default: notPortedAt(kStoryPort, "0x4dd1e0 routine", uint32_t(routine));
    }
}
// exe 0x4dd290
void bindScriptCast(AnimatedActor& actor, BonCastFields& cast) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnimShown;
    actor.flags |= kActorDrawn;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
    cast.ticks = 0;
    actor.state = uint8_t(ScriptCastState::ByVariant);
    actor.subState = 0;
    actor.restartStep();
}
void bonScriptCast(AnimatedActor& actor) {
    BonCastFields& cast = classState<BonCastFields>(actor);
    castHead(actor);
    const uint8_t state = actor.state;
    if (ScriptCastState(state) == ScriptCastState::Bind) {
        bindScriptCast(actor, cast);
    } else if (ScriptCastState(state) == ScriptCastState::ByVariant) { // 0x4dd300 [0x8957fc]
        if (actor.variant >= uint8_t(BonRoutine::Count)) {
            notPortedAt(kStoryPort, "0x4dd300 variant (table 0x8957fc)", actor.variant);
            return;
        }
        runRoutine(actor, BonRoutine(actor.variant));
    } else if (ScriptCastState(state) == ScriptCastState::Free) {
        freeActor(actor); // 0x468fa0
    } else if (state <= uint8_t(ScriptCastState::LastRoutine)) {
        runRoutine(actor, BonRoutine(state - uint8_t(ScriptCastState::FirstRoutine)));
    } else {
        notPortedAt(kStoryPort, "0x4dd1e0 state (table 0x8957f0)", state);
        return;
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1e80 / 0x5e1c30
}

} // namespace

void bonScriptCastUpdate(const PlayerEnv&, uint32_t actor) { bonScriptCast(animatedActorAt(actor)); }
void bonRetreatCastUpdate(const PlayerEnv&, uint32_t actor) { bonRetreatCast(animatedActorAt(actor)); }

} // namespace od::port
