// The burrowing reaverbot of the Manda ruins, class 0x482f50 (pool 0x20 type 0x22 sub 0 of ST12 / ST13 / ST14),
// content/manda/manda_ruin_burrower.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kBurrowerType = 0x22; // its animated pool type (the platform's rider, content/manda/manda_ruin_props)

// The burrower's spawn parameters (a platform riding it writes them: content/manda/manda_ruin_props)
struct BurrowerParams {
    explicit BurrowerParams(const AnimatedActor& actor) : BurrowerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit BurrowerParams(AnimatedActor& actor) : hopHeight(actor.params, 0), enemyId(actor.params[3]) {}
    BytesField<int16_t, 4> hopHeight; // the guard's hop above its ground (<= 0; its rider's platform lifts it)
    FieldAlias<int8_t, uint8_t> enemyId; // its encounter id (0: none)
};

// exe 0x482f50: the burrowing Manda-ruin reaverbot -- variant 0 crawls toward the player, rears up (two glow effects),
// leaps and bounces, spins / lies stunned, is knocked over; variant 1 stands guard, turns to face the player with
// hops and fires an aimed shot (pool 0x40 stage type 0) every 0x3c ticks; liftable / throwable, explodes
void mandaBurrowerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
