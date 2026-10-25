// The story in the Manda ruin (ST12 .. ST14, story 4): its scripts -- 9 (exe 0x518d60: the first guardian hall, ST13
// sub 1), 0xa (0x5191a0: after its guardian), 0xb (0x5195d0: the second guardian appears, ST12 sub 5), 0xc (0x519a20:
// after it), 0xf (0x51a980: the blue card at the terminal, ST12 sub 0), 0x1a (0x51ecc0: the last hall, ST14 sub 4)
// and 0xe (0x51a660: the ruin's guardian beaten) -- and their cast: a grounded figure (class exe 0x487c80, ST12 type
// 0x24 sub 2), the guardian's burning wreck (0x491ce0, ST14 type 0x2b sub 2) and the key item on its pedestal (static
// class 0x5b12c0, type 0x40). The room handlers that start them: content/manda/manda_rooms.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// the scripts by their handler (the story's dispatch, content/registry)
bool mandaRuinScriptPorted(RoutineId handler);
bool mandaRuinScriptTick(const PlayerEnv& env, RoutineId handler);

// exe 0x487c80 (record 0x89fb08 of script 0xa): a model on the ground, its animation asked from outside
void mandaRuinGroundedFigureUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x491ce0 (record 0x8a00e0 of script 0xe): moves along its yaw, random blasts for 0x82 ticks, two flames at the
// end, freed
void mandaRuinBurningWreckUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b12c0 (record 0x8a0108 of script 0xe): a key item on its pedestal (touched / used: event mode 2, its flag
// +0xe, freed)
void mandaRuinKeyItemUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
