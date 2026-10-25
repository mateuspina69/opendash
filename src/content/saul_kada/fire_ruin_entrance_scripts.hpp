// The fire ruin's story-13 cutscene scripts of ST27 / ST28: 0x40 / 0x41 / 0x42 / 0x43,
// content/saul_kada/fire_ruin_entrance_scripts.cpp. Their handler = [0x89b6d0 + id * 4].
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// handlers exe 0x52ce90 (0x40), 0x52d5a0 (0x41), 0x52de50 (0x42), 0x52e2e0 (0x43)
bool fireRuinEntranceScriptsPorted(RoutineId handler);
bool fireRuinEntranceScriptsTick(const PlayerEnv& env, RoutineId handler); // false if not ported

} // namespace od::port
