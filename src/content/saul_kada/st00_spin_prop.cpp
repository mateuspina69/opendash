// The ST00 prop (static type 1, exe 0x58d950; hypothesis names): the model of its variant (a missing model is not
// freed); variant 2 is drawn as scenery, variant 4 spins (its roll -0x80 a tick). States [0x8cd91c]: 0 bind 0x58d970,
// 1 run 0x58d9a0, 2 / 3 nothing.
#include "content/saul_kada/st00_spin_prop.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class PropState : uint8_t { Bind = 0, Run = 1, Idle = 2, IdleToo = 3 };
constexpr uint8_t kSceneryVariant = 2, kSpinningVariant = 4;

void spinProp(StaticActor& actor) {
    switch (PropState(actor.state)) {
    case PropState::Bind: // 0x58d970
        if (!bindActorModel(actor, actor.variant)) return; // exe 0x5e0bc0
        if (actor.variant == kSceneryVariant) actor.flags |= kActorScenery;
        ++actor.state;
        return;
    case PropState::Run: // 0x58d9a0
        if (actor.variant == kSpinningVariant) actor.rotation.z = int16_t((uint16_t(actor.rotation.z) - 0x80u) & 0xfff);
        return;
    case PropState::Idle:
    case PropState::IdleToo: return; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x58d950 state (table 0x8cd91c)", actor.state);
    }
}

} // namespace

void spinPropUpdate(const PlayerEnv&, uint32_t actor) { spinProp(staticActorAt(actor)); }

} // namespace od::port
