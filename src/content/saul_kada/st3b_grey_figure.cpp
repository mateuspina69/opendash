// The ST3B grey figure (pool 0x20 type 0xd sub 1, exe 0x46b990; hypothesis names): a still model with every part's
// bone scaled 1.0 and its third part hidden, hit points 1.
//
// States [0x8901bc]: 0 bind 0x46ba30 (box 0x8901b0), 1 nothing (0x52ef00). Its animation: the one asked started (exe
// 0x5e1c50 + 0x5e2d90), else stepped (0x5e1ea0 + 0x5e2d90).
#include "content/saul_kada/st3b_grey_figure.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

constexpr const char* kActorsPort = "st3b grey figure port"; // the area of its port-gap errors

enum class FigureState : uint8_t { Bind = 0, Still = 1 };
constexpr size_t kHiddenPart = 2;

// exe 0x46ba30
void bind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.anim = 0;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(TableId::St3bGreyFigureBox);
    actor.clearClassBlock();
    actor.parts[kHiddenPart].flags |= kPartHidden;
    resetBoneScales(actor); // exe 0x5e3220
    actor.enterState(uint8_t(FigureState::Still));
    actor.parts[kHiddenPart].flags |= kPartHidden;
}
// exe 0x46b990
void greyFigure(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (FigureState(actor.state)) { // [0x8901bc]
    case FigureState::Bind: bind(actor); break;
    case FigureState::Still: break; // 0x52ef00
    default: notPortedAt(kActorsPort, "0x46b990 state past [0x8901bc]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1c50 + 0x5e2d90 / 0x5e1ea0 + 0x5e2d90
}

} // namespace

void greyFigureUpdate(const PlayerEnv&, uint32_t actor) { greyFigure(animatedActorAt(actor)); }

} // namespace od::port
