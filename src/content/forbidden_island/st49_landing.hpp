// The landing on the Forbidden Island (ST49, reached from the travel map: its first pick of ST10 on map 1 sends the
// Flutter here, flag 0x5c0): script 0x18 (exe 0x51dd90) and its stage case, the Flutter seen from below as it comes
// down (class exe 0x49dd70, pool 0x20 type 0x30) and the stage's still model (static class exe 0x5b2960). The dropship
// of the scene: content/forbidden_island/st49_dropship. Bit-exact against tests/data/story_ST49_*.bin.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// script 0x18 by its handler (the story's dispatch, content/registry)
bool st49ScriptPorted(RoutineId handler);
bool st49ScriptTick(const PlayerEnv& env, RoutineId handler);
// the stage case of ST49 in the room scripts (flag 0x5c1 or 0x5c0 -> script 0x18)
bool st49RoomCaseKnown(uint8_t stage);
bool st49RoomCase();

// exe 0x49dd70 (states [0x8939e0]): the Flutter of the landing (state 1: sinks by 0x20 a tick while +0xc == 0)
void st49FlutterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b2960 (states [0x8d07b0]): a still model
void st49StillModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
