// The Manda ruin's hopping drone (ST13 pool 0x20 type 0x31 sub 1, exe 0x49f660), content/manda/hopping_drone.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kHoppingDroneType = 0x31, kHoppingDroneSub = 1; // its animated pool type / sub-type

// The drone's spawn parameters (its spawners write them: content/manda/ruin_spawners)
struct HoppingDroneParams {
    explicit HoppingDroneParams(const AnimatedActor& actor) : HoppingDroneParams(const_cast<AnimatedActor&>(actor)) {}
    explicit HoppingDroneParams(AnimatedActor& actor) : hopRange(actor.params, 0), partnerDied(actor.params[2]), partner(actor.params[3]) {}
    BytesField<int16_t, 4> hopRange;
    uint8_t& partnerDied; // set by the partner
    FieldAlias<int8_t, uint8_t> partner; // the partner's animated pool index
};

// exe 0x49f660: a drone that waits, then homes on the player in short hops (sidestepping to keep facing it, jumping at
// a jumping player), hurts it on contact; it can be lifted and thrown (hurting what it hits), is stunned by a fall and
// explodes into drops. Its variant may give it a flame (put out by the extinguisher) and a partner drone.
void hoppingDroneUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
