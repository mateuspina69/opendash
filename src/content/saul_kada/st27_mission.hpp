// ST27 sub 8 (the Saul Kada fire ruin's entrance, story 13): the lava-valve mission controller. See
// content/saul_kada/st27_mission.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x425560: pool 0xe0 type 0x17 (ST27) -- spawns the valve (the rock), Tron / Bon's fighting classes and the
// Reaverbots of the mission, keeps up to 6 pool-0x20 actors alive, the partners' HP warnings and the hint voices;
// the valve destroyed -> flag 0x4c
void lavaValveMissionUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
