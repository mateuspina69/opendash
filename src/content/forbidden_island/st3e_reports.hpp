// ST3E (sub 6): the talks of the story there -- script 0x64 at story 2 and the ruin-key reports, scripts 0x65 / 0x66 /
// 0x67 at stories 4 / 0xa / 0xd (a key reported: flags 0x3d0 / 0x3d1 / 0x3d2) -- their speakers (class exe 0x445660)
// and the stage's floating model (static class exe 0x5b16b0). The room handlers that start them:
// content/forbidden_island/island_rooms.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// scripts 0x64 .. 0x67 by their handler (the story's dispatch, content/registry)
bool st3eReportScriptPorted(RoutineId handler);
bool st3eReportScriptTick(const PlayerEnv& env, RoutineId handler);

// exe 0x445660 (pool 0x20 type 0): a speaker of the talks; its spawn parameters are the script's requests: +0xc the
// animation, +0xd a yaw offset, +0xe the turn step, +0xf the actor to face (a script actor's slot, >= 0x20 the player)
void st3eSpeakerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b16b0 (static): a model floating up and down, turning
void st3eFloatingModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
