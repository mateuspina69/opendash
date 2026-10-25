// The cutscene cast of the chapter's scripts (see town_story_cast.hpp): every member keys its moves on the running
// script's timeline (the step's sub-id and tick; a state k moves on to the next once the step sub-id reaches k). Their
// shadows (exe 0x5f20f0) are render output and left out.
#include "content/yosyonke/town_story_cast.hpp"
#include "content/yosyonke/town_story_cast_state.hpp"

#include "content/yosyonke/town_puppet.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace town_story_cast_state;

namespace {

constexpr const char* kCastPort = "town story port"; // the area of its port-gap errors

ScriptState& block() { return scriptState(); }
int32_t stepTick() { return block().stepTick; }
// the script's step sub-id reached `want` -> state `next`
void advanceAt(AnimatedActor& a, uint8_t want, uint8_t next) {
    if (block().stepSub == want) a.enterState(next);
}
// a point given by its integer x / z (the exe's stack 16.16 point: the low words 0) seen from the actor
int angleFromPoint(const AnimatedActor& a, int16_t x, int16_t z) {
    return angleOf(int32_t(uint32_t(uint16_t(x)) << 16), int32_t(uint32_t(uint16_t(z)) << 16), a.pos.x, a.pos.z);
}
void moveAlong(AnimatedActor& a, int yaw, int z) { moveAlongYaw(a.pos, yaw, 0, 0, z); } // exe 0x5e36f0
// the head's turn: its rate and the yaw it turns to
void lookAt(PuppetFields& f, int16_t rate, int16_t yaw) {
    f.headRate = rate;
    f.headTarget = yaw;
}
// the head straight (the state 0 of most of the cast)
void headStraight(PuppetFields& f) {
    f.headYaw = 0;
    f.headPitch = 0;
}

// ---- the junk shop (exe 0x447aa0, ST0A pool 0x20 type 0 sub 1; the variants 0x88e2b0) ---------------------------------
const DataTable<RoutineTable<3>> junkShopVariants{TableId::TownStoryCastJunkShopVariants};
constexpr uint32_t kFlagClerkSteps = 0x680; // the clerk steps back
// Roll: turns to the counter, walks there; at the end runs off (scene 7 / 8)
enum class JunkRollState : uint8_t {
    Bind = 0, Scene1 = 1, Scene2 = 2, TurnToCounter = 3, Scene4 = 4, WalkToCounter = 5, Scene6 = 6, Turn = 7,
    RunOff = 8,
};
enum class ClerkState : uint8_t { Bind = 0, Scene1 = 1, StepBack = 2, StepAside = 3, Scene4 = 4 };
enum class ThirdState : uint8_t { Bind = 0, Scene1 = 1, Placed = 2, Scene3 = 3, Scene4 = 4 };

void junkShopRoll(AnimatedActor& a, PuppetFields& f, int32_t t) {
    switch (JunkRollState(a.state)) {
    case JunkRollState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastJunkShopBox), 0)) return;
        f.puppetFlags &= uint8_t(~kPuppetResetAnim);
        headStraight(f);
        return;
    case JunkRollState::Scene1: advanceAt(a, 1, 2); return;
    case JunkRollState::Scene2: advanceAt(a, 2, 3); return;
    case JunkRollState::TurnToCounter:
        if (a.step == 0) {
            if (t == 0x1e) {
                f.headRate = 0x40;
                f.headOn = 1;
                f.headTarget = 0xda0;
                a.step = 1;
            }
        } else if (a.step == 1) {
            if (t == 0x28) a.step = 2;
        } else if (a.step == 2) {
            if (turnYawTo(a.rotation.y, 0x100, 0x40) == 0) a.step = 3;
        }
        advanceAt(a, 3, 4);
        return;
    case JunkRollState::Scene4: advanceAt(a, 4, 5); return;
    case JunkRollState::WalkToCounter: {
        const int ang = angleFromPoint(a, 0x50, -0x100);
        switch (a.step) {
        case 0:
            if (t == 0x28) a.step = 1;
            break;
        case 1:
            turnYawTo(a.rotation.y, uint32_t(ang), 0x40);
            if (t == 0x2d) {
                lookAt(f, 0x20, 0x400);
                a.anim = 1;
                a.step = 2;
            }
            break;
        case 2:
            turnYawTo(a.rotation.y, uint32_t(ang), 0x40);
            moveAlong(a, ang, -0x40);
            if (t == 0x64) a.step = 3;
            break;
        case 3:
            turnYawTo(a.rotation.y, 0x400, 0x20);
            moveAlong(a, ang, -0x40);
            if (t == 0x6e) {
                a.anim = 0;
                a.step = 4;
            }
            break;
        default: break;
        }
        advanceAt(a, 5, 6);
        return;
    }
    case JunkRollState::Scene6: advanceAt(a, 6, 7); return;
    case JunkRollState::Turn:
        if (a.step == 0 && block().subState == 4) {
            lookAt(f, 0x40, 0x500);
            a.step = 1;
        }
        advanceAt(a, 7, 8);
        return;
    case JunkRollState::RunOff: {
        const int ang = angleFromPoint(a, 0x240, -0x100);
        switch (a.step) {
        case 0:
            lookAt(f, 0x80, 0xf00);
            a.step = 1;
            [[fallthrough]];
        case 1:
            if (t == 2) a.step = 2;
            return;
        case 2:
            turnYawTo(a.rotation.y, uint32_t(ang), 0x100);
            if (t == 8) {
                lookAt(f, 0x80, 3 * kQuarterTurn);
                f.animKey = 6;
                a.anim = 2;
                a.step = 3;
            }
            return;
        case 3:
            turnYawTo(a.rotation.y, uint32_t(ang), 0x80);
            moveAlong(a, ang, -0x180);
            if (t == 0x32) a.step = 4;
            return;
        default: return;
        }
    }
    default: return;
    }
}
// the clerk: steps back once flag 0x680
void junkShopClerk(AnimatedActor& a, PuppetFields& f) {
    switch (ClerkState(a.state)) {
    case ClerkState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastJunkShopBox), 6)) return;
        f.puppetFlags &= uint8_t(~kPuppetResetAnim);
        headStraight(f);
        return;
    case ClerkState::Scene1: advanceAt(a, 1, 2); return;
    case ClerkState::StepBack: {
        uint8_t st = a.step;
        if (st == 0 && storyFlag(kFlagClerkSteps)) {
            a.anim = 7;
            a.step = 1;
            st = 1;
        }
        if (st == 1) {
            if (turnYawTo(a.rotation.y, 3 * kQuarterTurn, 0x80) == 0) a.step = 2;
            st = 2;
        }
        if (st == 2) moveAlong(a, 3 * kQuarterTurn, -0x40);
        advanceAt(a, 2, 3);
        return;
    }
    case ClerkState::StepAside: {
        uint8_t st = a.step;
        if (st == 0) {
            if (turnYawTo(a.rotation.y, 3 * kQuarterTurn, 0x80) == 0) a.step = 1;
            st = 1;
        }
        if (st == 1) moveAlong(a, 3 * kQuarterTurn, -0x40);
        advanceAt(a, 3, 4);
        return;
    }
    case ClerkState::Scene4: advanceAt(a, 4, 5); return;
    default: return;
    }
}
void junkShopThird(AnimatedActor& a, PuppetFields& f) {
    switch (ThirdState(a.state)) {
    case ThirdState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastJunkShopBox), 0)) return;
        headStraight(f);
        f.puppetFlags &= uint8_t(~kPuppetResetAnim);
        return;
    case ThirdState::Scene1: advanceAt(a, 1, 2); return;
    case ThirdState::Placed:
        if (a.subState == 0) {
            a.pos.x.whole = -0xc8;
            a.pos.y.whole = 0;
            a.pos.z.whole = -0xd0;
            a.subState = 1;
        }
        advanceAt(a, 2, 3);
        return;
    case ThirdState::Scene3: advanceAt(a, 3, 4); return;
    case ThirdState::Scene4: advanceAt(a, 4, 5); return;
    default: return;
    }
}
// exe 0x447aa0
void junkShopCast(AnimatedActor& a) {
    castSavePose(a);
    PuppetFields& f = classState<PuppetFields>(a);
    const int32_t t = stepTick();
    switch (junkShopVariants->idAt(size_t(int32_t(f.variant)))) {
    case RoutineId::TownCastJunkShopRoll: junkShopRoll(a, f, t); return;
    case RoutineId::TownCastJunkShopClerk: junkShopClerk(a, f); return;
    case RoutineId::TownCastJunkShopThird: junkShopThird(a, f); return;
    default: notPortedAt(kCastPort, "0x447aa0 variant", a.params[0]);
    }
}

const DataTable<RoutineTable<1>> runningRollVariants{TableId::TownStoryCastRunningRollVariants};
enum class RunningRollState : uint8_t { Bind = 0, Run = 1 };
enum class RunStep : uint8_t { Start = 0, Straight = 1, TurnOne = 2, TurnTwo = 3 };
// exe 0x448320
void runningRoll(const PlayerEnv& env, AnimatedActor& a) {
    castSavePose(a);
    RunningRollFields& f = classState<RunningRollFields>(a);
    if (runningRollVariants->idAt(size_t(int32_t(f.variant))) != RoutineId::TownCastRunningRoll)
        notPortedAt(kCastPort, "0x448320 variant", a.params[0]);
    if (RunningRollState(a.state) == RunningRollState::Bind) {
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastRunningRollBox), 2)) return;
        f.runFlags |= kPuppetResetAnim;
    } else if (RunningRollState(a.state) == RunningRollState::Run) {
        uint8_t st = a.step;
        if (RunStep(st) == RunStep::Start) {
            a.anim = 2;
            f.runFlags &= uint8_t(~kPuppetResetAnim);
            a.step = uint8_t(RunStep::Straight);
            st = uint8_t(RunStep::Straight);
        }
        const int32_t t = stepTick();
        if (RunStep(st) == RunStep::Straight) {
            integrateVelocity(a, 0, 0, -0x200);
            if (t == 0xa) a.step = uint8_t(RunStep::TurnOne);
        } else if (RunStep(st) == RunStep::TurnOne) {
            turnYawTo(a.rotation.y, 0xb00, 0x20);
            integrateVelocity(a, 0, 0, -0x200);
            if (t == 0x21) a.step = uint8_t(RunStep::TurnTwo);
        } else if (RunStep(st) == RunStep::TurnTwo) {
            turnYawTo(a.rotation.y, 0xf80, 0x20);
            integrateVelocity(a, 0, 0, -0x200);
        }
        advanceAt(a, 1, 2);
    }
    if (!(a.flags & kActorLive)) return;
    if (f.floating == 0) a.pos.y.whole = int16_t(actorGround(env, a));
    if (a.anim == a.prevAnim && !(f.runFlags & kPuppetResetAnim)) animStep(a); // exe 0x5e1e80 (+ the pose)
    else animSet(a, a.anim, f.animKey);                                         // exe 0x5e1c30 (+ the pose)
    animPose(a);
}

// ---- Joe's lab (exe 0x448520, ST08 sub 1 pool 0x20 type 0 sub 3; the variants 0x88e2e8: 0x448750 Roll, 0x448a20 Maria)
const DataTable<RoutineTable<2>> joesLabVariants{TableId::TownStoryCastJoesLabVariants};
enum class LabRollState : uint8_t { Bind = 0, WalkIn = 1, Seated = 2, Scene3 = 3, Turn = 4, LookUp = 5 };
enum class MariaState : uint8_t { Bind = 0, Scene3 = 1, Scene4 = 2 };

void joesLabRoll(AnimatedActor& a, PuppetFields& f, int32_t t) {
    switch (LabRollState(a.state)) {
    case LabRollState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastJoesLabBox), 0)) return;
        f.puppetFlags |= kPuppetResetAnim;
        headStraight(f);
        return;
    case LabRollState::WalkIn:
        switch (a.subState) {
        case 0:
            if (t == 0x14) {
                f.headOn = 1;
                lookAt(f, 0x40, 0x320);
                a.subState = 1;
            }
            break;
        case 1:
            turnYawTo(a.rotation.y, kHalfTurn, 0x60);
            if (t == 0x1c) {
                a.anim = 2;
                f.animKey = 6;
                f.puppetFlags &= uint8_t(~kPuppetResetAnim);
                ++a.subState;
            }
            break;
        case 2:
            turnYawTo(a.rotation.y, kHalfTurn, 0x40);
            moveAlong(a, kHalfTurn, -0x180);
            if (t == 0x20) ++a.subState;
            break;
        case 3:
            turnYawTo(a.rotation.y, kQuarterTurn, 0x40);
            moveAlong(a, kHalfTurn, -0x180);
            if (t == 0x28) ++a.subState;
            break;
        case 4:
            turnYawTo(a.rotation.y, kQuarterTurn, 0x40);
            moveAlong(a, kQuarterTurn, -0xc0);
            if (t == 0x50) ++a.subState;
            break;
        default: break;
        }
        advanceAt(a, 1, 2);
        return;
    case LabRollState::Seated:
        if (a.step == 0) {
            a.pos.x.whole = -0xf0;
            a.pos.y.whole = 0;
            a.pos.z.whole = 0x80;
            a.rotation.y = 0xff0;
            a.anim = 0;
            f.animKey = 0;
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
            f.headYaw = 0;
            f.headPitch = -0x200;
            f.headRate = 0;
            f.headTarget = 0;
            f.headHeight = 0;
            a.step = 1;
        } else if (a.step == 1 && block().stepPhase == 4) {
            lookAt(f, 0x60, 0xd00);
            f.headHeight = -0x200;
            a.step = 2;
        }
        advanceAt(a, 2, 3);
        return;
    case LabRollState::Scene3: advanceAt(a, 3, 4); return;
    case LabRollState::Turn:
        if (block().stepPhase == 0) a.rotation.y = 0xd80;
        advanceAt(a, 4, 5);
        return;
    case LabRollState::LookUp:
        if (a.step == 0) {
            lookAt(f, 0x60, 0xc50);
            f.headHeight = 0;
            a.step = 1;
        } else if (a.step == 1) {
            if (turnYawTo(a.rotation.y, 0xd20, 0x60) == 0) a.step = 2;
        }
        return;
    default: return;
    }
}
void joesLabMaria(AnimatedActor& a, PuppetFields& f) {
    switch (MariaState(a.state)) {
    case MariaState::Bind:
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastJoesLabBox), 0)) return;
        f.puppetFlags |= kPuppetResetAnim;
        headStraight(f);
        lookAt(f, 0x40, 0);
        f.headHeight = 0x200;
        return;
    case MariaState::Scene3: advanceAt(a, 3, 2); return;
    case MariaState::Scene4: advanceAt(a, 4, 3); return;
    default: return;
    }
}
// exe 0x448520
void joesLabCast(AnimatedActor& a) {
    castSavePose(a);
    PuppetFields& f = classState<PuppetFields>(a);
    const int32_t t = stepTick();
    switch (joesLabVariants->idAt(size_t(int32_t(f.variant)))) {
    case RoutineId::TownCastJoesLabRoll: joesLabRoll(a, f, t); return;
    case RoutineId::TownCastJoesLabMaria: joesLabMaria(a, f); return;
    default: notPortedAt(kCastPort, "0x448520 variant", a.params[0]);
    }
}

// ---- ST47's person (exe 0x4474e0, ST47 pool 0x20 type 0 sub 1; the variants [0x88e278 + variant * 4]) ----------------
// variant 0 = exe 0x447710, the person of script 0x12 (keyed on the script's sub-state = the message shown): turns to
// 0xf00 / 0xb40 / 0xf00, the head toward 0x1d0 / 0xb40 / 0x1d0 / 0xf00, then walks off -z 0x180 a tick
const DataTable<RoutineTable<1>> st47PersonVariants{TableId::TownStoryCastSt47PersonVariants};
enum class PersonState : uint8_t { Bind = 0, Talk = 1 };
enum class PersonSub : uint8_t { Wait = 0, TurnOne = 1, TurnTwo = 2, TurnThree = 3, WalkOff = 4 };
// the head toward `yaw` (rate 0x80, level), the next sub-state
void personLook(AnimatedActor& a, PuppetFields& f, int16_t yaw) {
    lookAt(f, 0x80, yaw);
    f.headHeight = 0;
    ++a.subState;
}
// exe 0x4474e0
void st47Person(AnimatedActor& a) {
    castSavePose(a);
    PuppetFields& f = classState<PuppetFields>(a);
    const size_t variant = size_t(int32_t(f.variant));
    if (st47PersonVariants->idAt(variant) != RoutineId::TownCastSt47Person)
        notPortedAt(kCastPort, "0x4474e0 variant", st47PersonVariants->pointerAt(variant));
    if (PersonState(a.state) == PersonState::Bind) {
        if (!puppetBind(a, stateAddress(TableId::TownStoryCastSt47PersonBox), 0)) return;
        f.puppetFlags |= kPuppetResetAnim;
        headStraight(f);
        f.headTarget = 0xb40;
        f.headHeight = 0;
        f.headOn = 1;
        return;
    }
    if (PersonState(a.state) != PersonState::Talk) return;
    const uint8_t shown = block().subState;
    switch (PersonSub(a.subState)) {
    case PersonSub::Wait:
        if (shown == 2) personLook(a, f, 0x1d0);
        return;
    case PersonSub::TurnOne:
        turnYawTo(a.rotation.y, 0xf00, 0x60);
        if (block().subState == 3) personLook(a, f, 0xb40);
        return;
    case PersonSub::TurnTwo:
        turnYawTo(a.rotation.y, 0xb40, 0x60);
        if (block().subState == 5) personLook(a, f, 0x1d0);
        return;
    case PersonSub::TurnThree:
        turnYawTo(a.rotation.y, 0xf00, 0x40);
        if (block().subState == 6) {
            personLook(a, f, 0xf00);
            a.anim = 2;
            f.animKey = 6;
            f.puppetFlags &= uint8_t(~kPuppetResetAnim);
        }
        return;
    case PersonSub::WalkOff:
        turnYawTo(a.rotation.y, 0, 0x40);
        moveAlong(a, 0, -0x180);
        return;
    default: return;
    }
}

// ---- the junk shop's prop (exe 0x5915b0, ST0A static type 1 sub 1; its states 0x8cde2c: 0x5915d0 the model of its
// variant (none: freed), 0x591600 variant 0 slides +x 0x80 a tick until the script's tick 0x2d) ---------------------
enum class JunkPropState : uint8_t { Bind = 0, Slide = 1 };
enum class SlideSub : uint8_t { Sliding = 0, Done = 1 };
void junkShopProp(StaticActor& a) {
    if (JunkPropState(a.state) == JunkPropState::Bind) {
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.state = uint8_t(JunkPropState::Slide);
        return;
    }
    if (JunkPropState(a.state) != JunkPropState::Slide) notPortedAt(kCastPort, "0x5915b0 state", a.state);
    if (a.variant == 0 && SlideSub(a.subState) == SlideSub::Sliding) {
        integrateVelocity(a, 0x80, 0, 0);
        if (stepTick() == 0x2d) a.subState = uint8_t(SlideSub::Done);
    }
}

} // namespace

void junkShopCastUpdate(const PlayerEnv& env, uint32_t actor) {
    AnimatedActor& a = animatedActorAt(actor);
    junkShopCast(a);
    if (a.flags & kActorLive) puppetTail(env, a);
}
void rollRunsOffUpdate(const PlayerEnv& env, uint32_t actor) { runningRoll(env, animatedActorAt(actor)); }
void joesLabCastUpdate(const PlayerEnv& env, uint32_t actor) {
    AnimatedActor& a = animatedActorAt(actor);
    joesLabCast(a);
    if (a.flags & kActorLive) puppetTail(env, a);
}
void st47PersonUpdate(const PlayerEnv& env, uint32_t actor) {
    AnimatedActor& a = animatedActorAt(actor);
    st47Person(a);
    if (a.flags & kActorLive) puppetTail(env, a);
}
void junkShopPropUpdate(const PlayerEnv&, uint32_t actor) { junkShopProp(staticActorAt(actor)); }

} // namespace od::port
