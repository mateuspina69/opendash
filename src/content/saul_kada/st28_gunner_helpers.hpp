// The fire-ruin gunner boss's two helpers (ST28 pool 0x20 type 0xb sub 1, exe 0x4617a0), see
// content/saul_kada/st28_gunner_helpers.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kGunnerHelperType = 0xb, kGunnerHelperSubtype = 1;
// The helper's spawn parameter (the boss writes it with its own address): its side (0 / 1)
struct GunnerHelperParams {
    explicit GunnerHelperParams(const AnimatedActor& actor) : GunnerHelperParams(const_cast<AnimatedActor&>(actor)) {}
    explicit GunnerHelperParams(AnimatedActor& actor) : side(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> side;
};

// exe 0x4617a0: a helper standing at the boss's side: lobbed shots at the player, the attack when the boss's beam caught
// him, knocked out / liftable / throwable, getting up again
void gunnerHelperUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
