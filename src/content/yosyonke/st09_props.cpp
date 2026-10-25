// The ST09 still model, static class 0x5b6a20: a model by its variant, no collision. Its states come from the exe's
// table 0x8d0a40 (bind 0x5b6a40, idle 0x52ef00, freed 0x5afdd0), dispatched by the routine's address; an entry past
// the class's own states (the exe's tables run into the next class's) is a port gap.
#include "content/yosyonke/st09_props.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class StillModelState : uint8_t {
    Bind = 0, // the model by the variant; a failure stays here (no free)
    Idle = 1,
    Freed = 2,
};
const DataTable<RoutineTable<3>> stillModelStates{TableId::St09PropsStillModelStates};

// exe 0x5b6a20
void stillModel(StaticActor& actor) {
    switch (stillModelStates->idAt(actor.state)) {
    case RoutineId::St09StillModelBind:
        if (bindActorModel(actor, actor.variant)) actor.enterState(uint8_t(StillModelState::Idle)); // exe 0x5e0bc0
        return;
    case RoutineId::EmptyRoutine: return;
    case RoutineId::StaticFreedState: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "static 0x5b6a20 state", actor.state);
    }
}

} // namespace

void stillModelUpdate(const PlayerEnv&, uint32_t actor) { stillModel(staticActorAt(actor)); }

} // namespace od::port
