// The dropship parked in the Flutter's hangar (see st07_hangar_dropship.hpp).
#include "content/yosyonke/st07_hangar_dropship.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

constexpr const char* kHangarArea = "island story port"; // the area of its port-gap errors

enum class HangarDropshipState : uint8_t {
    Bind = 0,   // exe 0x4f2a80
    Parked = 1, // exe 0x52ef00 (nothing)
    Free = 2,   // exe 0x468fa0
};
constexpr uint8_t kParkedAnim = 2;

// exe 0x4f29e0: the head (the previous animation, the tint, the previous pose and contact), the state, then the
// animation while it is drawn
void hangarDropship(AnimatedActor& a) {
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (HangarDropshipState(a.state)) {
    case HangarDropshipState::Bind:
        if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
            freeActor(a);
            return;
        }
        a.flags |= kActorDrawn;
        a.anim = kParkedAnim;
        a.prevAnim = kNoAnimShown;
        a.collisionBox = nullptr;
        a.interactionBox = nullptr;
        a.interaction = kInteractLift;
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.airborne = 0;
        a.onPlatform = 0;
        a.platform = nullptr;
        a.actorHit = 0;
        a.state = uint8_t(HangarDropshipState::Parked);
        break;
    case HangarDropshipState::Parked: break;
    case HangarDropshipState::Free: freeActor(a); break;
    default: notPortedAt(kHangarArea, "0x4f29e0 state", a.state);
    }
    if (!(a.flags & kActorDrawn)) return;
    if (a.anim != a.prevAnim) animSet(a, a.anim, 0); // exe 0x5e1c50(a, +0xa0, 0)
    else animStep(a);                                // exe 0x5e1ea0
    animPose(a);                                     // exe 0x5e2d90
}

} // namespace

void hangarDropshipUpdate(const PlayerEnv&, uint32_t actor) { hangarDropship(animatedActorAt(actor)); }

} // namespace od::port
