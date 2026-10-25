// The first visit of the Forbidden Island (story 1, saves 01 / 00): the room-script handlers of ST10 (the landing
// field), ST11 and ST1D sub 0 (blizzard layer 0x431af0, the actor groups, the arrival messages, the exit locks), and
// the classes they spawn, one routine per exe routine on the actor views.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// Room-script handlers ([[table + story * 4] + sub * 4](0xa714e0)): ST10 0x57dec0 / 0x57dfb0 (story 1),
// 0x57e000 / 0x57e060 (story 2..); ST11 0x57e180 (story 1), 0x57e310 (2..); ST1D 0x5804b0 (story 1 sub 0),
// 0x5806e0 (story 2.. sub 2). run = false only asks whether fn is known (game/room_scripts roomScriptHandler)
bool islandVisitRoomHandler(RoutineId fn, bool run);

// ST10 pool 0x20 type 0x2c (0x492270): the dropship parked on the island (bind, animation 0, then animate)
void parkedDropshipUpdate(const PlayerEnv& env, uint32_t actor);
// ST10 pool 0x20 type 0x37 (0x4acbe0): a two-part decoration of sub 1 (variant 1 = model type 0x68), animated
void islandDecorationUpdate(const PlayerEnv& env, uint32_t actor);
// ST10 pool 0x60 type 0x1d (0x5a37c0): sub 1's statics; variant 0 blocks its grid cell (variant 1), kept on the ground
void cellBlockerUpdate(const PlayerEnv& env, uint32_t actor);
// ST1D pool e0 type 0x24 (0x427440): a hidden reaverbot (pool 0x20 type 5 variant 2) popping out near the player
void hiddenReaverbotUpdate(const PlayerEnv& env, uint32_t actor);
// ST1D pool e0 type 0x11 (0x424d10): the timed type-0x34 sub-1 spawns while the player is in the pack's area
void reaverbotPackUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
