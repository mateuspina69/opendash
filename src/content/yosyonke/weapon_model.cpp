// The special weapon's turning model, class 0x4ec4f0 (pool 0x20 type 0x65 of ST04 / ST19, spawned by the special-weapon
// screens 0x545b80 / 0x548380 / 0x5edac0): state 0 binds the model by the variant and starts animation 0; then a delay
// counts up by the game's pace to 0x1f, after which the yaw turns 0x20 a tick.
#include "content/yosyonke/weapon_model.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/display.hpp"
#include "game/field_alias.hpp"

namespace od::port {

namespace {

enum class WeaponModelState : uint8_t {
    Bind = 0, // the model, animation 0, the delay cleared
    Shown = 1,
};

// the class's bytes: the delay before the model turns
struct WeaponModelFields {
    explicit WeaponModelFields(const AnimatedActor& actor) : WeaponModelFields(const_cast<AnimatedActor&>(actor)) {}
    explicit WeaponModelFields(AnimatedActor& actor) : delay(actor.params, 2) {}
    BytesField<uint16_t, 4> delay;
};

constexpr uint8_t kWeaponModelDrawBits = uint8_t(kActorDrawn | 0x20); // drawn, and bit 5 of +0 (its reader is not
                                                                       // traced)
constexpr uint16_t kTurnDelay = 0x1e;  // the model turns once the delay is past this
constexpr uint16_t kTurnStep = 0x20;   // the yaw's turn per tick

// exe 0x4ec4f0
void weaponModel(AnimatedActor& actor) {
    WeaponModelFields f(actor);
    if (WeaponModelState(actor.state) == WeaponModelState::Bind) {
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.flags |= kWeaponModelDrawBits;
        animSetPose(actor, 0, 0);
        f.delay = 0;
        ++actor.state;
    }
    if (f.delay <= kTurnDelay) {
        f.delay += display().pace;
        animStepPose(actor);
        return;
    }
    actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + uint32_t(kTurnStep)) & kAngleMask);
    animStepPose(actor);
}

} // namespace

void weaponModelUpdate(const PlayerEnv&, uint32_t actor) { weaponModel(animatedActorAt(actor)); }

} // namespace od::port
