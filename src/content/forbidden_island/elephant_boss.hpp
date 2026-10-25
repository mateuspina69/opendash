// The Forbidden Island boss chapter (save 00: ST1D, story 1): the crystal on its pedestal (ST1D static type 1), the
// floor collapse (script 0x28), the giant elephant reaverbot (ST1D pool 0x20 type 0x34: sub 3 in the cutscene, sub 2
// the boss), its rocks / shock rings / dust (statics 0x20, pool 0x40 type 0x14, effect 0x11), the arena's pillars
// (static 0x20 sub 1) and exit light (static 0x28), and the aftermath (script 0x29: the request to ST1C). One routine
// per exe routine on the actor views, bit-exact against tests/data/story_ST1D_*.bin. The names are hypotheses from
// the observed behaviour.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// Scripts 0x28 (0x524a30: the crystal -> the floor caves in -> the arena) and 0x29 (0x525480: the boss is down ->
// request 2 to ST1C sub 0)
bool elephantScriptPorted(RoutineId handler);
bool elephantScriptTick(const PlayerEnv& env, RoutineId handler);

// Actor classes (registered in game/actors)
void st1dCrystalUpdate(const PlayerEnv& env, uint32_t actor);      // 0x590260 ST1D static type 1
void elephantCutsceneUpdate(const PlayerEnv& env, uint32_t actor); // 0x4a9770 ST1D pool 0x20 type 0x34 sub 3
void elephantBossUpdate(const PlayerEnv& env, uint32_t actor);     // 0x4a4fd0 ST1D pool 0x20 type 0x34 sub 2
void st1dRockUpdate(const PlayerEnv& env, uint32_t actor);         // 0x5a51c0 ST1D static type 0x20 sub 0
void st1dPillarUpdate(const PlayerEnv& env, uint32_t actor);       // 0x5a5530 ST1D static type 0x20 sub 1
void st1dExitLightUpdate(const PlayerEnv& env, uint32_t actor);    // 0x5aa060 ST1D static type 0x28
void st1dBoulderUpdate(const PlayerEnv& env, uint32_t actor);      // 0x5c7780 ST1D pool 0x40 type 0x14
void st1dDustRingUpdate(const PlayerEnv& env, uint32_t actor);     // 0x4372b0 ST1D effect type 0x11
// exe 0x5c7e00 alone (tests): the shock ring's 0xf8 draw command at the cursor 0x6d0050 (game/draw_commands)
void st1dShockRingCommand(uint32_t actor);


} // namespace od::port
