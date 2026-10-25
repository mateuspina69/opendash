// The throwable object (static type 9 of the global table: a breaking wall's pieces, game/breakable_walls): it falls
// and bounces off the walls along its launch, rests (blinking its last ticks before it goes), can be lifted, carried
// and thrown by the player; thrown it hurts what it hits until it lands broken.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// its launch (written by its spawners): the pitch / yaw of the actor's rotation and the speed along them, kept in the
// rotation's spare lane
struct ThrowableLaunch {
    explicit ThrowableLaunch(const StaticActor& actor) : ThrowableLaunch(const_cast<StaticActor&>(actor)) {}
    explicit ThrowableLaunch(StaticActor& actor) : speed(actor.rotation.spare) {}
    int16_t& speed;
};

// exe 0x564690 (static global type 9; states 0x8c4380)
void throwableUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
