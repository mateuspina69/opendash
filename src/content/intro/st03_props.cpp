// The tilted model of ST03 (see st03_props.hpp): its state 0 binds the model (none: freed), points the collision box at
// its record and tilts it; nothing after.
#include "content/intro/st03_props.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"

namespace od::port {

namespace {

constexpr int16_t kStillModelTilt = 0x50;     // its roll
enum class StillModelState : uint8_t { Bind = 0, Still = 1 };

} // namespace

// exe 0x5a7af0: the state 0 only
void st03TiltedModelUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& a = staticActorAt(address);
    if (StillModelState(a.state) != StillModelState::Bind) return;
    if (!bindActorModel(a, 0)) {
        freeActor(a); // exe 0x5e1540
        return;
    }
    a.collisionBox.pointAt(TableId::St03PropsStillModelBox);
    a.enterState(uint8_t(StillModelState::Still));
    a.rotation.x = 0;
    a.rotation.z = kStillModelTilt;
}

} // namespace od::port
