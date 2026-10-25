// The ST05 / ST06 props: the talk props and the hanging bar (static class 0x58dbc0), the swaying / spinning
// decorations (static class 0x58ddf0) and the posed figure (pool 0x20 class 0x481000).
#include "content/yosyonke/st05_st06_props.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/player/player_hang.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

constexpr uint32_t kTalkEvent = 0x12; // the talk props' event (mode 0, flags 0x12)

// ---- the talk props and the hanging bar (exe 0x58dbc0) --------------------------------------------------------------
// Variants 0 / 1 / 3 / 7: "use" -> the message [0x8cd99c + variant] (event 0x12), solid except variant 0; variant 2: a
// bar spinning (yaw += 0x18 a tick) and moving -0x80 a tick that Megaman hangs from (exe 0x573bd0), held 0x28 ahead
enum class TalkPropState : uint8_t {
    Bind = 0, // exe 0x58dc10: the model, the talk box by the variant
    Run = 1,  // exe 0x58dca0 (the variants' table 0x58dd64); 2 / 3: exe 0x52ef00 (nothing)
};
// the bar's sub-state (+9)
enum class BarSub : uint8_t { Start = 0, Spinning = 1 };
// the talk step (+0xa) of the props, the grab step of the bar
enum class TalkStep : uint8_t { Waiting = 0, Talking = 1 };
enum class BarStep : uint8_t { Free = 0, Held = 1 };
constexpr uint8_t kBarVariant = 2;
constexpr int16_t kBarDrift = -0x80, kBarSpin = 0x18, kBarHoldAhead = 0x28;
const DataTable<ScalarTable<uint8_t, 8>> talkPropMessages{TableId::St05St06PropsTalkPropMessages};
const DataTable<InteractionBoxRecord> talkBoxSmall{TableId::St05St06PropsTalkBoxSmall}; // variant 0
const DataTable<InteractionBoxRecord> talkBoxBar{TableId::St05St06PropsTalkBoxBar}; // the bar's grab box
const DataTable<InteractionBoxRecord> talkBoxDefault{TableId::St05St06PropsTalkBoxDefault}; // the other variants
constexpr uint8_t kInteractBar = 0x08;        // the bar's interaction byte (its reader is not traced)

// exe 0x58dd90: "use" starts the prop's message; the step back once the event ended
void propTalk(StaticActor& actor, uint8_t message) {
    if (TalkStep(actor.step) == TalkStep::Waiting) {
        if (!player().talksTo(actor) || eventState().active != 0) return;
        eventStartAtActor(actor, kTalkEvent, 0, message); // exe 0x543af0
        ++actor.step;
    } else if (eventState().active == 0) {
        actor.step = uint8_t(TalkStep::Waiting);
    }
}

// exe 0x58dbc0
void talkProp(StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    const uint8_t s = actor.state;
    const uint8_t v = actor.variant;
    if (TalkPropState(s) == TalkPropState::Bind) {
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        actor.interaction = kInteractTalk;
        if (v == 0) {
            actor.interactionBox.pointAt(talkBoxSmall.stateAddress());
        } else if (v == kBarVariant) {
            actor.interactionBox.pointAt(talkBoxBar.stateAddress());
            actor.interaction = kInteractBar;
        } else {
            actor.interactionBox.pointAt(talkBoxDefault.stateAddress());
        }
        actor.state = uint8_t(TalkPropState::Run);
        actor.subState = 0;
        actor.step = 0;
        return;
    }
    if (TalkPropState(s) != TalkPropState::Run) return;
    switch (v) {
    case 0: propTalk(actor, talkPropMessages[0]); return;
    case 1:
    case 3:
    case 7:
        propTalk(actor, talkPropMessages[v]);
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, -1, 0)
        return;
    case kBarVariant:
        if (BarSub(actor.subState) == BarSub::Start) {
            actor.subState = uint8_t(BarSub::Spinning);
            actor.velocity.z = kBarDrift;
            actor.acceleration.y = kBarSpin; // the bar keeps its spin in the acceleration's y lane
            return;
        }
        actor.rotation.y += actor.acceleration.y;
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        if (BarStep(actor.step) == BarStep::Free) {
            if (playerBarGrab(actor) == 1) ++actor.step;
        } else if (playerBarGrab(actor) == 2) {
            playerPlaceAt(actor, 0, 0, 0, kBarHoldAhead); // exe 0x573d70(a, 0, 0, 0, 0x28)
        } else {
            actor.step = uint8_t(BarStep::Free);
        }
        return;
    default: return;
    }
}

// ---- the swaying / spinning decorations (exe 0x58ddf0) ---------------------------------------------------------------
// By variant, a tick counter: 0 rocks (the roll from the 64-step sine >> 5); 1 spins with a speed that ramps up / down
// and flips direction every 256 ticks; 2 builds the shared scratch transform (the pitch from the sine of the exe table
// >> 6) that variant 3 copies into its own root (kActorPoseOnRoot) while turning 0x80 a tick; 6 / 7 swing the yaw (7
// also rolls -0x100 a tick). The rebind state draws variants 2 / 3 as scenery.
enum class DecorState : uint8_t {
    Bind = 0,   // exe 0x58de40
    Rebind = 4, // exe 0x58de80
    Move = 5,   // exe 0x58dec0 (the variants' table 0x58dfc4)
    // 1 / 2 / 3 / 6 / 7: exe 0x52ef00 (nothing)
};
// the decorations' bytes
struct DecorFields {
    explicit DecorFields(const StaticActor& actor) : DecorFields(const_cast<StaticActor&>(actor)) {}
    explicit DecorFields(StaticActor& actor) : ticks(actor.params[0]), reversed(actor.params[1]) {}
    uint8_t& ticks;
    uint8_t& reversed; // variant 1: the spin's direction
};
// the decorations keep their spin speed in spin.y
enum class DecorVariant : uint8_t {
    Rocking = 0, Spinning = 1, SharedPitch = 2, OnSharedPitch = 3, Swinging = 6, SwingingRolling = 7,
};
constexpr int16_t kDecorTurn = 0x80;
constexpr uint16_t kRollStep = 0xff00; // -0x100 a tick

int16_t swayOfTicks(uint8_t c) { return int16_t(sin64(c) >> 5); }

// exe 0x58ddf0
void decoration(StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (DecorState(actor.state)) {
    case DecorState::Bind:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.state = 1;
        actor.flags |= kActorDrawn;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        return;
    case DecorState::Rebind:
        actor.enterState(1);
        if (DecorVariant(actor.variant) == DecorVariant::SharedPitch ||
            DecorVariant(actor.variant) == DecorVariant::OnSharedPitch)
            actor.flags |= kActorScenery;
        if (bindActorModel(actor, actor.variant)) actor.spin.y = 0;
        return;
    case DecorState::Move: {
        DecorFields f(actor);
        const uint8_t c = uint8_t(f.ticks + 1u);
        f.ticks = c;
        const uint8_t v = actor.variant;
        switch (DecorVariant(v)) {
        case DecorVariant::Rocking: actor.rotation.z = swayOfTicks(c); return;
        case DecorVariant::Spinning:
            if (c == 0) {
                f.reversed ^= 1u;
                ++actor.spin.y;
            } else if (c & 0x80) {
                --actor.spin.y;
            } else {
                ++actor.spin.y;
            }
            if (f.reversed) actor.rotation.y += actor.spin.y;
            else actor.rotation.y -= actor.spin.y;
            return;
        case DecorVariant::SharedPitch:
            actor.rotation.x = int16_t(fixedSin(int((c & 0x7fu) << 5)) >> 6);
            projectionScratch().transform.setRotationYXZ(actor.rotation); // exe 0x40bfd0
            return;
        case DecorVariant::OnSharedPitch:
            actor.rotation.y += kDecorTurn;
            actor.flags |= kActorPoseOnRoot;
            {
                uint32_t words[8];
                for (size_t i = 0; i < 8; ++i) words[i] = projectionScratch().transformWords[i];
                actor.setRootWords(words);
            }
            return;
        case DecorVariant::Swinging:
        case DecorVariant::SwingingRolling:
            actor.rotation.y = int16_t((uint32_t(uint16_t(swayOfTicks(c))) - uint32_t(kHalfTurn)) & kAngleMask);
            if (DecorVariant(v) == DecorVariant::SwingingRolling) {
                actor.rotation.z += kRollStep;
                if (actor.rotation.z < 0) actor.rotation.z += 0x1000u;
            }
            return;
        default: return;
        }
    }
    default: return;
    }
}

// ---- the posed figure (exe 0x481000, pool 0x20) ------------------------------------------------------------------
// States (the exe's table 0x89193c): 0 bind 0x4810a0 (model variant 0, the talk box 0x891930), 1 nothing, 2 freed,
// 3 / 4 = 0x481210 / 0x481510 (the cast routines of 0x481100, content/intro/st03_attract_cast): called here with one
// argument, they would read their block from the frame body's esi (the game-mode block 0xa6d180) -- nothing sets those
// states; left visible if ever reached
enum class PosedFigureState : uint8_t {
    Bind = 0,  // exe 0x4810a0
    Idle = 1,  // exe 0x52ef00
    Freed = 2, // exe 0x468fa0
};
const DataTable<InteractionBoxRecord> figureTalkBox{TableId::St05St06PropsFigureTalkBox};

// exe 0x481000
void posedFigure(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (PosedFigureState(actor.state)) {
    case PosedFigureState::Bind:
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        actor.prevAnim = 0xff;
        actor.flags |= kActorDrawn;
        actor.anim = 0;
        actor.collisionBox = nullptr;
        actor.interactionBox.pointAt(figureTalkBox.stateAddress());
        actor.interaction = kInteractTalk;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        actor.state = uint8_t(PosedFigureState::Idle);
        break;
    case PosedFigureState::Idle: break;
    case PosedFigureState::Freed: freeActor(actor); return;
    default:
        notPortedAt(kStoryPort, "0x481000 state (table 0x89193c; 3 / 4 read the game-mode block as their script block)",
                    actor.state);
        return;
    }
    if (!(actor.flags & kActorDrawn)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    animPose(actor);                                   // exe 0x5e2d90
}

} // namespace

void posedFigureUpdate(const PlayerEnv&, uint32_t actor) { posedFigure(animatedActorAt(actor)); }
void townTalkPropUpdate(const PlayerEnv&, uint32_t actor) { talkProp(staticActorAt(actor)); }
void decorationUpdate(const PlayerEnv&, uint32_t actor) { decoration(staticActorAt(actor)); }

} // namespace od::port
