// ST33 (the fire ruin, story 13): script 0x2f, which drops the key item after the guardian's second fight, its figure
// and the key item. See content/saul_kada/st33_key_script.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b1cd0 (static ST33 type 0x40 sub 0): the key item; touched: the item-get event (message 0x1e) and flag 0x3ed
// (the room handler then clears the door locks)
void keyItemUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4ccf50 (pool 0x20 ST33 type 0x45 sub 2, record 0x8a5ad0): the guardian figure of script 0x2f
void keyScriptFigureUpdate(const PlayerEnv& env, uint32_t actor);
// script 0x2f (handler exe 0x527360; the room handler starts it at flag 0x4a): the guardian's collapse, the key item
// dropped from its bone 3; the end places the item and the player
bool keyDropScriptPorted(RoutineId handler);
bool keyDropScriptTick(const PlayerEnv& env, RoutineId handler); // false if not ported

} // namespace od::port
