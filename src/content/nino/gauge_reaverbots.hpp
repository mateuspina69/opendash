// The gauge reaverbots of the Nino ruin (pool 0x20 type 0x76: ST37 sub 6's fight, ST44, ST50), their mines and the
// cast of script 0x5f, content/nino/gauge_reaverbots.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4fb670 (sub 0, states [0x897a3c]): the reaverbot with a boss gauge (its colour by its second parameter): it
// swims (the player in water) or stands / walks, circles the player, dashes, shoots the homing shot and lays mines; the
// last of the group down (with its variant set) drops ST37's key
void gaugeReaverbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4fd180 (sub 1, states [0x897ab4]): the floating mine the reaverbot lays: it bobs 0x300 ticks or until hit, then
// a blast
void floatingMineUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4fd600 (sub 2): the cast of script 0x5f (the camera script spawns them), by its first parameter: 0 swims to the
// room's centre, 1 stands; both glow at the script's cues
void gaugeCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
