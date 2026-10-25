// The title screen's model (see title_model.hpp).
#include "game/actor_state.hpp"
#include "game/title_model.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

inline constexpr const char* kTitleModelArea = "title model";

// the spawn parameter: set, the model's own colour tables are not used
struct TitleModelFields {
    explicit TitleModelFields(const AnimatedActor& actor) : TitleModelFields(const_cast<AnimatedActor&>(actor)) {}
    explicit TitleModelFields(AnimatedActor& actor) : plainColours(actor.params[0]) {}
    uint8_t& plainColours;
};
enum class TitleModelState : uint8_t { Bind = 0, Animate = 1 }; // exe 0x42a850 / 0x492320
constexpr int16_t kTitleScale = 0x800;
constexpr uint32_t kTitleAnimation = 1;

} // namespace

void titleModelUpdate(const PlayerEnv&, uint32_t address) {
    AnimatedActor& actor = animatedActorAt(address);
    switch (TitleModelState(actor.state)) { // [0x88c540]
    case TitleModelState::Bind:
        if (!bindActorModel(actor, actor.variant)) return; // exe 0x5e0bc0
        if (TitleModelFields(actor).plainColours != 0) actor.flags &= uint8_t(~kActorColourTables);
        actor.scale.x = kTitleScale;
        actor.scale.y = kTitleScale;
        actor.scale.z = kTitleScale;
        animSetPose(actor, kTitleAnimation, 0); // exe 0x5e1c30
        actor.enterState(uint8_t(TitleModelState::Animate));
        return;
    case TitleModelState::Animate: animStepPose(actor); return; // exe 0x5e1e80
    default: notPortedAt(kTitleModelArea, "0x42a830 state", actor.state);
    }
}

} // namespace od::port
