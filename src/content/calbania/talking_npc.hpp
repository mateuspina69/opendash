// The standing NPCs you talk to, classes 0x449620 (pool 0x20, ST24 type 1: the story NPC of ST24 story 3) and
// 0x4499a0 (its twin of ST17 type 1, record 0x8c75d8, story 7..: other boxes, the hints from the stage's message bank);
// the exe shares their wait state 0x449850 (content/calbania/talking_npc.cpp). The talk can open the save menu (event
// flag 0x6fa, game/save_menus) or the hints window (flag 0x6f9).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x449620: the ST24 talking NPC's update (ActorUpdateFn)
void talkingNpcUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4499a0: the ST17 twin's update (ActorUpdateFn)
void talkingNpcSt17Update(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
