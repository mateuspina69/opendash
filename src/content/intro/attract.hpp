// The attract intro (what plays before the title when its idle timer runs out: the main task's mode 4 loads GAME.DAT
// with ST02 room 0): its two room cases that start its scripts and the dispatch of the scripts (content/intro/attract.cpp,
// registered in content/registry.cpp) -- script 3 in ST02 (content/intro/st02_attract_script.cpp), then script 4 in
// ST03 (content/intro/st03_attract_script.cpp), which ends in the main task's switch back to the title.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// the attract's script handlers (exe 0x5129e0 script 3, 0x513740 script 4)
bool attractScriptPorted(RoutineId handler);
bool attractScriptTick(const PlayerEnv& env, RoutineId handler);
// the room-script cases of ST02 (exe 0x57bb30) / ST03 (exe 0x57bb90); false: another stage
bool attractRoomCase();

// the scripts' state routines (content/intro/st02_attract_script.cpp, st03_attract_script.cpp)
void st02AttractScript(); // exe 0x5129e0
void st03AttractScript(); // exe 0x513740
// the warp of the attract's steps: the in-stage request to `stage` room `room` (no fade)
void attractWarp(uint8_t stage, uint8_t room);
// the scripts' end: the block ended, every sound stopped, the main task back to the title (exe 0x604b06(0x542ff0):
// the game task ends here; the app reacts to the main task's mode)
void attractBackToTitle();

} // namespace od::port
