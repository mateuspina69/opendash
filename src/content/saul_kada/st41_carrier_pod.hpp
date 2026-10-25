// The ST41 / ST42 carrier's pods (static type 0x4f, exe 0x5b6be0), see content/saul_kada/st41_carrier_pod.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b6be0: dropped by the carrier (content/saul_kada/st41_carrier): falls, sways, lunges at the player with a
// glow, climbs back to the carrier on its signal
void carrierPodUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
