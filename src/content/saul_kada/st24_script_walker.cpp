// The ST24 script walker (pool 0x20 type 0x3a, exe 0x4b2650; hypothesis names): a cast member that walks in once its
// script has started (0x3f ticks, its pose held), plays its animations by the script's step sub-id (1 / 3 / 4) and step
// tick, and in step 4 turns away, walks off and hides at tick 0x64; kept on the ground.
// States [0x894518]: 0 bind 0x4b2710, 1 act 0x4b27c0 (the acts [0x894520]); the table goes on with the acts as states
// 2..5.
#include "content/saul_kada/st24_script_walker.hpp"
#include "content/saul_kada/st24_script_walker_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st24_script_walker_state;

namespace {

enum class WalkerState : uint8_t { Bind = 0, Act = 1, WalkInAct = 2, FirstAct = 3, SecondAct = 4, LeaveAct = 5 };
// [0x894520]
enum class WalkerAct : uint8_t { WalkIn = 0, First = 1, Second = 2, Leave = 3 };
enum class WalkInStep : uint8_t { Start = 0, WaitForScript = 1, Walk = 2, Stop = 3 };
enum class LeaveStep : uint8_t { Turn = 0, TurnAway = 1, WalkOff = 2, Gone = 3 };
constexpr int16_t kWalkSpeed = -0x50;
constexpr uint16_t kWalkInTicks = 0x3f;

// exe 0x4b27f0: walks in once the script has started
void walkIn(AnimatedActor& actor, WalkerFields& walker) {
    switch (WalkInStep(actor.step)) {
    case WalkInStep::Start:
        walker.walkTicks = kWalkInTicks;
        walker.holdPose = 1;
        actor.step = uint8_t(WalkInStep::WaitForScript);
        [[fallthrough]];
    case WalkInStep::WaitForScript:
        if (scriptState().part<ScriptCastStart>().started == 0) return;
        actor.step = uint8_t(WalkInStep::Walk);
        walker.holdPose = 0;
        [[fallthrough]];
    case WalkInStep::Walk:
        integrateVelocity(actor, 0, 0, kWalkSpeed); // exe 0x5e3690
        if (walker.walkTicks) --walker.walkTicks;
        if (walker.walkTicks == 0 && actor.animEnded()) {
            actor.anim = 1;
            actor.step = uint8_t(WalkInStep::Stop);
        }
        return;
    case WalkInStep::Stop:
        if (int8_t(actor.keyIndex) < 0xa) integrateVelocity(actor, 0, 0, kWalkSpeed);
        if (actor.animEnded()) {
            actor.subState = uint8_t(WalkerAct::First);
            actor.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x4b28c0: the step sub-id 1's animations
void firstAct(AnimatedActor& actor) {
    if (scriptState().stepSub != 1) return;
    const int32_t tick = scriptState().stepTick;
    if (tick == 0) actor.anim = 2;
    else if (tick == 0x2e) actor.anim = 3;
    else if (tick == 0xb4) actor.anim = 4;
    else if (tick == 0xdc) actor.anim = 5;
    else if (tick == 0x100) {
        actor.anim = 6;
        actor.subState = uint8_t(WalkerAct::Second);
        actor.restartStep();
    }
}
// exe 0x4b2940: the step sub-id 3's turn and animation
void secondAct(AnimatedActor& actor) {
    if (scriptState().stepSub != 3) return;
    const int32_t tick = scriptState().stepTick;
    if (tick == 0) {
        actor.rotation.y += 0x400u;
        actor.anim = 7;
    } else if (tick == 0x104) {
        actor.anim = 8;
        actor.subState = uint8_t(WalkerAct::Leave);
        actor.restartStep();
    }
}
// exe 0x4b2990: the step sub-id 4: turns away, walks off, hidden at tick 0x64
void leave(AnimatedActor& actor) {
    if (scriptState().stepSub != 4) return;
    if (actor.step <= uint8_t(LeaveStep::TurnAway)) {
        if (actor.animEnded()) {
            actor.pos.x.whole += 0x49u;
            actor.pos.z.whole -= 0x2bu;
            actor.rotation.y += 0x700u;
            actor.anim = 0;
            actor.step = uint8_t(LeaveStep::WalkOff);
        }
    } else if (LeaveStep(actor.step) == LeaveStep::WalkOff) {
        integrateVelocity(actor, 0, 0, kWalkSpeed);
        if (scriptState().stepTick == 0x64) {
            actor.step = uint8_t(LeaveStep::Gone);
            actor.setDrawn(false);
        }
    }
}
void act(AnimatedActor& actor, WalkerFields& walker, WalkerAct which) {
    switch (which) {
    case WalkerAct::WalkIn: walkIn(actor, walker); return;
    case WalkerAct::First: firstAct(actor); return;
    case WalkerAct::Second: secondAct(actor); return;
    case WalkerAct::Leave: leave(actor); return;
    default: return;
    }
}

// exe 0x4b2710
void bind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    const uint16_t hp =
        hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type).hitPoints << 4) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::St24ScriptWalkerBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.clearClassBlock();
    integrateVelocity(actor, 0, 0, 0xa00);
    actor.enterState(uint8_t(WalkerState::Act));
}

// exe 0x4b2650
void scriptWalker(const PlayerEnv& env, AnimatedActor& actor) {
    WalkerFields& walker = classState<WalkerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint8_t state = actor.state;
    if (WalkerState(state) == WalkerState::Bind) { // 0x4b2710
        bind(actor);
    } else if (WalkerState(state) == WalkerState::Act) { // 0x4b27c0
        if (actor.subState >= 4) {
            notPortedAt(kStoryPort, "0x4b27c0 sub-state (table 0x894520 runs into other code)", actor.subState);
            return;
        }
        act(actor, walker, WalkerAct(actor.subState));
        actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    } else if (state >= uint8_t(WalkerState::WalkInAct) && state <= uint8_t(WalkerState::LeaveAct)) {
        act(actor, walker, WalkerAct(state - uint8_t(WalkerState::WalkInAct)));
    } else {
        notPortedAt(kStoryPort, "0x4b2650 state (table 0x894518)", state);
        return;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);
    else if (walker.holdPose) animDecodeCurrent(actor); // exe 0x5e1f30
    else animStepPose(actor);                           // exe 0x5e1e80
}

} // namespace

void scriptWalkerUpdate(const PlayerEnv& env, uint32_t actor) { scriptWalker(env, animatedActorAt(actor)); }

} // namespace od::port
