// Appo and Dah's face (see appo_dah_face.hpp).
#include "content/calbania/appo_dah_face.hpp"
#include "game/actor_state.hpp"

namespace od::port {

namespace {

constexpr uint8_t kMouthTicks = 3; // a step shows 4 ticks

void showPart(AnimatedActor& actor, size_t part) { actor.parts[part].flags &= uint8_t(~kPartHidden); }
void hidePart(AnimatedActor& actor, size_t part) { actor.parts[part].flags |= kPartHidden; }

} // namespace

void appoDahFaceReset(AnimatedActor& actor, AppoDahMouth& mouth) {
    hidePart(actor, kMouthWidePart);
    hidePart(actor, kMouthOpenPart);
    showPart(actor, kMouthClosedPart);
    mouth.step = 0;
    mouth.ticks = 0;
}

void appoDahTalk(AnimatedActor& actor, AppoDahMouth& mouth) {
    const uint8_t t = mouth.ticks;
    if (t != 0) {
        mouth.ticks = uint8_t(t - 1u);
        return;
    }
    mouth.ticks = kMouthTicks;
    switch (mouth.step & 7) {
    case 0:
    case 6:
        hidePart(actor, kMouthClosedPart);
        showPart(actor, kMouthWidePart);
        break;
    case 1:
        hidePart(actor, kMouthWidePart);
        showPart(actor, kMouthOpenPart);
        break;
    case 2:
        hidePart(actor, kMouthOpenPart);
        showPart(actor, kMouthWidePart);
        break;
    case 3:
    case 7:
        hidePart(actor, kMouthWidePart);
        showPart(actor, kMouthClosedPart);
        break;
    case 4:
        hidePart(actor, kMouthClosedPart);
        showPart(actor, kMouthOpenPart);
        break;
    case 5:
        hidePart(actor, kMouthOpenPart);
        showPart(actor, kMouthClosedPart);
        break;
    default: break;
    }
    ++mouth.step;
}

} // namespace od::port
