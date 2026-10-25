// Tron Bonne at Pokte Village (ST25, story 3 -> 4): script 0x1b (exe 0x51f0c0: Tron at the village's gate, subs 5 /
// 6) and script 0x1c (exe 0x520050: after her tank is beaten, sub 6), and their cast -- Tron in her cockpit (class exe
// 0x45af20), her tank as it lands (0x471520), its wreck (0x471990) and the figure of script 0x1c (0x45bce0). The room
// handlers that start them: content/manda/pokte_rooms; the fight: content/calbania/tron_boss. Notes:
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// scripts 0x1b / 0x1c by their handler (the story's dispatch, content/registry)
bool st25TronScriptPorted(RoutineId handler);
bool st25TronScriptTick(const PlayerEnv& env, RoutineId handler);

// exe 0x45af20 (sub 5, pool 0x20; hypothesis: Tron in her cockpit): her poses and faces keyed on script 0x1b's steps
void st25TronUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x471520 (sub 6, pool 0x20; hypothesis: Tron's tank as it lands in the village): the drop, the dust, the stand
void st25TankLandingUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x471990 (sub 6, pool 0x20 type 0x17; script 0x1c's record 0x8a3028; hypothesis: the wreck of Tron's tank): its
// parts' explosions by the step tick, then it sinks, tilts and fades out
void st25TankWreckUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x45bce0 (pool 0x20 type 9 sub 1; script 0x1c's record 0x8a303c): its actions by the script's step sub (hops
// off in arcs, sparks, drives)
void st25Script1cFigureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
