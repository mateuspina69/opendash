// The player's status markers (static types 0x12 / 0x13 of the global table, spawned by the status effects,
// game/player/player_status): the drain / burn marker -- sprites on the player's bones, rising particles, the player
// tinted -- and the slow marker -- lightning between the player's bones and sparks. Each lasts while its status does
// (and the player is settled), then fades and clears its marker bit.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// the drain / burn marker's variant: bit 0 the drain (else the burn), bit 7 it starts at its full level
constexpr uint8_t kMarkerOfDrain = 0x01;
constexpr uint8_t kMarkerStartsFull = 0x80;

// exe 0x5684a0 (static global type 0x12; states 0x5684c0 / 0x568630 / 0x568700): the drain / burn marker
void drainBurnMarkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x568b40 (static global type 0x13; states 0x568b60 / 0x568c10 / 0x568cb0): the slow marker
void slowMarkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
