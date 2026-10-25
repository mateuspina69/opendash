// The gate of the Manda ruin (ST24 sub 1, story 4): script 0x14 (exe 0x51d140), the cutscene where a Manda guardian
// appears at the gate (class exe 0x487d50, pool 0x20 type 0x24 sub 0) before the request to the ruin (ST12 sub 0).
// The room handler that starts it: content/manda/pokte_rooms.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// script 0x14 by its handler (the story's dispatch, content/registry)
bool st24GateScriptPorted(RoutineId handler);
bool st24GateScriptTick(const PlayerEnv& env, RoutineId handler);

// exe 0x487d50 (script 0x14's record 0x8a1e2c, slot 1): the guardian fades in with dust, plays its animations by the
// script's steps, hides, then fades out
void st24GateGuardianUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
