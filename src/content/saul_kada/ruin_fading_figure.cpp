// A figure of the fire ruin's room scripts that fades in (ST27 pool 0x20 type 0x53 sub 1, exe 0x4df230; hypothesis
// names), spawned by the room-script records 0x8a5618 (two records, their list at 0x8a56c4); its sibling sub 0 is the
// digger 0x4dd9d0 (content/manda/exam_enemies_b), which does not spawn it.
// States [0x8958a0]: 0 bind 0x4df2d0, 1 run 0x4df350, 2 nothing (0x52ef00), 3 = its sub-state 0x4df3b0 (the table runs
// on into [0x8958ac]). The run: its tint from its colour bytes, the fade (0x4df430) while fading, its sub-state. The
// shadow is render output.
#include "content/saul_kada/ruin_fading_figure.hpp"
#include "content/saul_kada/ruin_fading_figure_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace ruin_fading_figure_state;

namespace {

enum class FigureState : uint8_t { Bind = 0, Run = 1, Idle = 2, Wait = 3 };
enum class FigureAnim : uint8_t { Idle = 0, Appear = 5 };
constexpr uint8_t kFading = 0x01;
constexpr int16_t kFigureScale = 0x180;

// exe 0x4df430 (its jump table 0x4df4f8, by the step counter): semi-transparent, the blue up to 0x10, then red / green
// up to 0x10 over 0x14 ticks, opaque again
void fade(AnimatedActor& actor, FigureFields& figure) {
    switch (actor.stepCounter) {
    case 0:
        actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00
        actor.flags |= kActorDrawn;
        ++actor.stepCounter;
        return;
    case 1:
        if (figure.blue >= 0x10) {
            figure.blue = 0x10;
            ++actor.stepCounter;
            figure.fadeTimer = 0x14;
        } else {
            figure.blue += 1u;
        }
        return;
    case 2:
        if (figure.fadeTimer != 0) {
            figure.fadeTimer -= 1u;
            if (figure.red == 0x10) return;
            figure.red += 1u;
            figure.green += 1u;
            return;
        }
        actor.semiTransparency = semiTransparencyBits(0, 1);
        ++actor.stepCounter;
        return;
    case 3:
        actor.stepCounter = 0;
        figure.fading &= uint8_t(~kFading);
        return;
    default: return;
    }
}
// exe 0x4df3b0: the wait, then appearing (fading in), then idle
void waitAndAppear(AnimatedActor& actor, FigureFields& figure) {
    switch (actor.step) {
    case 0:
        if (figure.wait != 0) {
            figure.wait -= 1u;
            return;
        }
        figure.fading |= kFading;
        actor.anim = uint8_t(FigureAnim::Appear);
        ++actor.step;
        return;
    case 1:
        if (!(figure.fading & kFading)) {
            actor.anim = uint8_t(FigureAnim::Idle);
            actor.step = 2;
        } else if (actor.animEnded()) {
            actor.anim = uint8_t(FigureAnim::Idle);
        }
        return;
    default: return;
    }
}
// exe 0x4df2d0
void bind(AnimatedActor& actor, FigureFields& figure) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.scale.x = kFigureScale;
    actor.scale.y = kFigureScale;
    actor.scale.z = kFigureScale;
    actor.tint = uint16_t(kFigureScale); // the exe's dword store over the scale's z and the tint
    actor.anim = uint8_t(FigureAnim::Idle);
    actor.prevAnim = 0xff;
    figure.fading = 0;
    figure.blue = 1;
    figure.green = 1;
    figure.red = 1;
    figure.fadeTimer = 0;
    figure.wait = 0x1e;
    actor.state = uint8_t(FigureState::Run);
    actor.subState = 0;
    actor.step = 0;
    actor.stepCounter = 0;
    actor.tint = kTintNeutral;
}
// exe 0x4df350
void run(AnimatedActor& actor, FigureFields& figure) {
    actor.tint = uint16_t(((uint32_t(figure.blue) << 5 | figure.green) << 5) | figure.red);
    if (figure.fading & kFading) fade(actor, figure);
    if (actor.subState != 0) notPortedAt(kStoryPort, "0x4df350: sub-state past [0x8958ac]", actor.subState);
    waitAndAppear(actor, figure);
}
void fadingFigure(AnimatedActor& actor) { // exe 0x4df230
    FigureFields& figure = classState<FigureFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (FigureState(actor.state)) { // [0x8958a0]
    case FigureState::Bind: bind(actor, figure); break;
    case FigureState::Run: run(actor, figure); break;
    case FigureState::Idle: break; // exe 0x52ef00
    case FigureState::Wait: waitAndAppear(actor, figure); break;
    default: notPortedAt(kStoryPort, "0x4df230: state past its tables", actor.state);
    }
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else animStepPose(actor);                                            // exe 0x5e1e80
    // drawn: the shadow (exe 0x5f20f0(actor, 0x60, 0xc0, 0)) -- render output
}

} // namespace

void fadingFigureUpdate(const PlayerEnv&, uint32_t actor) { fadingFigure(animatedActorAt(actor)); }

} // namespace od::port
