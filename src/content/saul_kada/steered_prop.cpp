// The steered prop (static ST2F / ST58 type 0x39, exe 0x5af6d0; hypothesis names): the model of its spawn byte; each
// tick its yaw / pitch step 0x40 toward their targets (12-bit wrap), its tint the RGB555 of its colour, or white for one
// tick when flashed. States [0x8cf9c4]: 0 bind 0x5af6f0, 1 steer 0x5af720, 2 nothing.
#include "content/saul_kada/steered_prop.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class PropState : uint8_t { Bind = 0, Steer = 1, Idle = 2 };
constexpr int16_t kTurnStep = 0x40;

// one step of an angle toward its target (12-bit wrap of the difference and of the result)
uint16_t turnStep(uint16_t current, uint16_t target) {
    int16_t d = int16_t(current - target);
    if (d >= 0x800) d = int16_t(d - 0x1000);
    if (d < -0x800) d = int16_t(d + 0x1000);
    uint16_t v = d >= kTurnStep ? uint16_t(current - kTurnStep) : d <= -kTurnStep ? uint16_t(current + kTurnStep) : target;
    if (int16_t(v) >= 0x800) v = uint16_t(v - 0x1000);
    if (int16_t(v) < -0x800) v = uint16_t(v + 0x1000);
    return v;
}

void steeredProp(StaticActor& actor) {
    switch (PropState(actor.state)) {
    case PropState::Bind: // 0x5af6f0
        if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.state = uint8_t(PropState::Steer);
        return;
    case PropState::Steer: { // 0x5af720
        SteeredPropParams params(actor);
        SteeredPropFields& targets = classState<SteeredPropFields>(actor);
        actor.savePreviousPose();
        if (params.flash == 0) {
            actor.tint = uint16_t((((int32_t(params.blue.get()) << 5) | params.green) << 5) | params.red);
        } else {
            params.flash = 0;
            actor.tint = kTintFlash;
        }
        actor.rotation.y = int16_t(turnStep(uint16_t(actor.rotation.y), targets.targetYaw));
        actor.rotation.x = int16_t(turnStep(uint16_t(actor.rotation.x), targets.targetPitch));
        return;
    }
    case PropState::Idle: return; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x5af6d0 state (table 0x8cf9c4)", actor.state);
    }
}

} // namespace

void steeredPropUpdate(const PlayerEnv&, uint32_t actor) { steeredProp(staticActorAt(actor)); }

} // namespace od::port
